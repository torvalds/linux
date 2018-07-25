ifeq ($(KBUILD_TOP),)
    ifneq ($(KBUILD_EXTMOD),)
    KBUILD_DIR := $(KBUILD_EXTMOD)
    else
    KBUILD_DIR := $(PWD)
    endif
KBUILD_TOP := $(KBUILD_DIR)/../
endif

include $(KBUILD_TOP)/config.mak

KBUILD_EXTRA_SYMBOLS += $(KBUILD_TOP)/ssvdevice/Module.symvers

#Define CONFIG_CABRIO_DEBUG to show debug messages
ccflags-y += -DCONFIG_CABRIO_DEBUG

KMODULE_NAME=ssv6200_sdiobridge
KERN_SRCS := sdiobridge.c
KERN_SRCS += debug.c


include $(KBUILD_TOP)/rules.mak
