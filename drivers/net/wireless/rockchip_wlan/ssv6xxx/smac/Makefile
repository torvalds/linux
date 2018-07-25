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
KBUILD_EXTRA_SYMBOLS += $(KBUILD_TOP)/hci/Module.symvers

KMODULE_NAME=ssv6200s_core
KERN_SRCS := init.c
KERN_SRCS += dev.c
KERN_SRCS += ssv_rc.c
KERN_SRCS += ssv_ht_rc.c
KERN_SRCS += lib.c
KERN_SRCS += ap.c
KERN_SRCS += ampdu.c
KERN_SRCS += ssv6xxx_debugfs.c
KERN_SRCS += sec_ccmp.c
KERN_SRCS += sec_tkip.c
KERN_SRCS += sec_wep.c
KERN_SRCS += wapi_sms4.c
KERN_SRCS += sec_wpi.c
KERN_SRCS += efuse.c
KERN_SRCS += p2p.c
KERN_SRCS += sar.c
include $(KBUILD_TOP)/rules.mak
