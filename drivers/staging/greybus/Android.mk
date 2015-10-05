.PHONY: build-greybus

ifneq ($(TARGET_NO_KERNEL), true)
$(PRODUCT_OUT)/ramdisk.img: build-greybus
endif

GREYBUS_MODULE_OUT_PATH := $(PRODUCT_OUT)/root/lib/modules
GREYBUS_CC_PREFIX := $(shell cd `dirname $(TARGET_TOOLS_PREFIX)` && pwd)/$(shell basename $(TARGET_TOOLS_PREFIX))

include $(CLEAR_VARS)
GREYBUS_SRC_PATH := $(ANDROID_BUILD_TOP)/external/greybus/
LOCAL_PATH := $(GREYBUS_SRC_PATH)
LOCAL_SRC_FILES := greybus.ko
LOCAL_MODULE := $(LOCAL_SRC_FILES)
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(GREYBUS_MODULE_OUT_PATH)
$(LOCAL_PATH)/$(LOCAL_SRC_FILES): build-greybus
include $(BUILD_PREBUILT)

KDIRARG := KERNELDIR="${ANDROID_PRODUCT_OUT}/obj/kernel"
ARCHARG := ARCH=$(TARGET_ARCH)
FLAGARG := EXTRA_CFLAGS+=-fno-pic
ARGS := $(KDIRARG) $(ARCHARG) $(FLAGARG)

build-greybus: $(INSTALLED_KERNEL_TARGET)
	make clean -C $(GREYBUS_SRC_PATH)
	cd $(GREYBUS_SRC_PATH) &&\
	$(MAKE) -j$(MAKE_JOBS) CROSS_COMPILE=$(GREYBUS_CC_PREFIX) $(ARGS)
	mkdir -p $(GREYBUS_MODULE_OUT_PATH)
	ko=`find $(GREYBUS_SRC_PATH) -type f -name "*.ko"`;\
	for i in $$ko; do $(GREYBUS_CC_PREFIX)strip --strip-unneeded $$i;\
	mv $$i $(GREYBUS_MODULE_OUT_PATH)/; done;
