LOCAL_PATH := $(call my-dir)

INCLUDES = $(LOCAL_PATH)
INCLUDES += $(LOCAL_PATH)/../../src/utils
INCLUDES += $(LOCAL_PATH)/../../src/common
INCLUDES += $(LOCAL_PATH)/../../src
INCLUDES += external/libxml2/include
INCLUDES += external/curl/include
INCLUDES += external/webkit/Source/WebKit/gtk

# We try to keep this compiling against older platform versions.
# The new icu location (external/icu) exports its own headers, but
# the older versions in external/icu4c don't, and we need to add those
# headers to the include path by hand.
ifeq ($(wildcard external/icu),)
INCLUDES += external/icu4c/common
else
# The LOCAL_EXPORT_C_INCLUDE_DIRS from ICU did not seem to fully resolve the
# build (e.g., "mm -B" failed to build, but following that with "mm" allowed
# the build to complete). For now, add the include directory manually here for
# Android 5.0.
ver = $(filter 5.0%,$(PLATFORM_VERSION))
ifneq (,$(strip $(ver)))
INCLUDES += external/icu/icu4c/source/common
endif
endif


L_CFLAGS += -DCONFIG_CTRL_IFACE
L_CFLAGS += -DCONFIG_CTRL_IFACE_UNIX
L_CFLAGS += -DCONFIG_CTRL_IFACE_CLIENT_DIR=\"/data/misc/wifi/sockets\"

OBJS = spp_client.c
OBJS += oma_dm_client.c
OBJS += osu_client.c
OBJS += est.c
OBJS += ../../src/common/wpa_ctrl.c
OBJS += ../../src/common/wpa_helpers.c
OBJS += ../../src/utils/xml-utils.c
#OBJS += ../../src/utils/browser-android.c
OBJS += ../../src/utils/browser-wpadebug.c
OBJS += ../../src/utils/wpabuf.c
OBJS += ../../src/utils/eloop.c
OBJS += ../../src/wps/httpread.c
OBJS += ../../src/wps/http_server.c
OBJS += ../../src/utils/xml_libxml2.c
OBJS += ../../src/utils/http_curl.c
OBJS += ../../src/utils/base64.c
OBJS += ../../src/utils/os_unix.c
L_CFLAGS += -DCONFIG_DEBUG_FILE
OBJS += ../../src/utils/wpa_debug.c
OBJS += ../../src/utils/common.c
OBJS += ../../src/crypto/crypto_internal.c
OBJS += ../../src/crypto/md5-internal.c
OBJS += ../../src/crypto/sha1-internal.c
OBJS += ../../src/crypto/sha256-internal.c
OBJS += ../../src/crypto/tls_openssl_ocsp.c

L_CFLAGS += -DEAP_TLS_OPENSSL

L_CFLAGS += -Wno-unused-parameter


########################
include $(CLEAR_VARS)
LOCAL_MODULE := hs20-osu-client
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := libc libcutils
LOCAL_SHARED_LIBRARIES += libcrypto libssl
#LOCAL_SHARED_LIBRARIES += libxml2
LOCAL_STATIC_LIBRARIES += libxml2
LOCAL_SHARED_LIBRARIES += libicuuc
LOCAL_SHARED_LIBRARIES += libcurl

LOCAL_CFLAGS := $(L_CFLAGS)
LOCAL_SRC_FILES := $(OBJS)
LOCAL_C_INCLUDES := $(INCLUDES)
include $(BUILD_EXECUTABLE)

########################
