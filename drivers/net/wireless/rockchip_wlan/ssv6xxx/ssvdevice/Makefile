ifeq ($(KBUILD_TOP),)
    ifneq ($(KBUILD_EXTMOD),)
    KBUILD_DIR := $(KBUILD_EXTMOD)
    else
    KBUILD_DIR := $(PWD)
    endif
KBUILD_TOP := $(KBUILD_DIR)/../
endif

include $(KBUILD_TOP)/config.mak


KMODULE_NAME=ssvdevicetype
KERN_SRCS := ssvdevice.c
KERN_SRCS += ssv_cmd.c


include $(KBUILD_TOP)/rules.mak
