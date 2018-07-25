ifeq ($(KBUILD_TOP),)
    ifneq ($(KBUILD_EXTMOD),)
    KBUILD_DIR := $(KBUILD_EXTMOD)
    else
    KBUILD_DIR := $(PWD)
    endif
KBUILD_TOP := $(KBUILD_DIR)/../
endif

include $(KBUILD_TOP)/config.mak

KBUILD_EXTRA_SYMBOLS += $(KBUILD_TOP)/hci/Module.symvers


KMODULE_NAME=hci_wrapper
KERN_SRCS := ssv_huw.c


include $(KBUILD_TOP)/rules.mak
