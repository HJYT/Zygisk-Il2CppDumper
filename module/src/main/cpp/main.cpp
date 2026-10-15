#include <cstring>
#include <thread>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <sys/types.h>
#include <unistd.h>
#include <cinttypes>
#include "hack.h"
#include "zygisk.hpp"
#include "log.h"

// 运行时目标包名。编译不再写死，启动游戏前执行：
//   su -c setprop dump.il2cpp <包名>
// 属性为空或与当前进程不一致时卸载模块。重启后属性会丢失，需要重新设置。

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

class MyModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        const char *package_name = nullptr;
        const char *app_data_dir = nullptr;
        if (args->nice_name != nullptr) {
            package_name = env->GetStringUTFChars(args->nice_name, nullptr);
        }
        if (args->app_data_dir != nullptr) {
            app_data_dir = env->GetStringUTFChars(args->app_data_dir, nullptr);
        }
        preSpecialize(package_name, app_data_dir);
        if (package_name != nullptr) {
            env->ReleaseStringUTFChars(args->nice_name, package_name);
        }
        if (app_data_dir != nullptr) {
            env->ReleaseStringUTFChars(args->app_data_dir, app_data_dir);
        }
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (enable_hack) {
            std::thread hack_thread(hack_prepare, game_data_dir, data, length);
            hack_thread.detach();
        }
    }

private:
    Api *api;
    JNIEnv *env;
    bool enable_hack = false;
    char *game_data_dir = nullptr;
    void *data = nullptr;
    size_t length = 0;

    void preSpecialize(const char *package_name, const char *app_data_dir) {
        char prop[PROP_VALUE_MAX] = {};
        __system_property_get("dump.il2cpp", prop);
        if (prop[0] != '\0' && package_name != nullptr && app_data_dir != nullptr &&
            strcmp(package_name, prop) == 0) {
            LOGI("detect game: %s", package_name);
            enable_hack = true;
            game_data_dir = new char[strlen(app_data_dir) + 1];
            strcpy(game_data_dir, app_data_dir);

#if defined(__i386__)
            auto path = "zygisk/armeabi-v7a.so";
#endif
#if defined(__x86_64__)
            auto path = "zygisk/arm64-v8a.so";
#endif
#if defined(__i386__) || defined(__x86_64__)
            int dirfd = api->getModuleDir();
            int fd = openat(dirfd, path, O_RDONLY);
            if (fd != -1) {
                struct stat sb{};
                fstat(fd, &sb);
                length = sb.st_size;
                data = mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, 0);
                close(fd);
            } else {
                LOGW("Unable to open arm file");
            }
#endif
        } else {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        }
    }
};

REGISTER_ZYGISK_MODULE(MyModule)