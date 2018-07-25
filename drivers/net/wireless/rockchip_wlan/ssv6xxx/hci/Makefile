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


KMODULE_NAME=ssv6200_hci
KERN_SRCS += ssv_hci.c


include $(KBUILD_TOP)/rules.mak
