KMODULE_NAME=ssv6051

KBUILD_TOP := drivers/net/wireless/rockchip_wlan/ssv6xxx

KERNEL_TOP := $(PWD)
FIRMWARE_PATH := $(KERNEL_TOP)/../vendor/rockchip/common/wifi/firmware
KO_PATH := $(KERNEL_TOP)/../device/rockchip/common/wifi/modules/

ifeq ($(KERNELRELEASE),)
# current directory is driver
CFGDIR = $(PWD)/../../../../config

-include $(CFGDIR)/build_config.cfg
-include $(CFGDIR)/rules.make

endif

include $(KBUILD_TOP)/ssv6051.cfg
include $(KBUILD_TOP)/platform-config.mak


EXTRA_CFLAGS := -I$(KBUILD_TOP) -I$(KBUILD_TOP)/include
DEF_PARSER_H = $(KBUILD_TOP)/include/ssv_conf_parser.h
$(shell env ccflags="$(ccflags-y)" $(KBUILD_TOP)/parser-conf.sh $(DEF_PARSER_H))

KERN_SRCS := ssvdevice/ssvdevice.c
KERN_SRCS += ssvdevice/ssv_cmd.c

KERN_SRCS += hci/ssv_hci.c

KERN_SRCS += smac/init.c
KERN_SRCS += smac/dev.c
KERN_SRCS += smac/ssv_rc.c
KERN_SRCS += smac/ssv_ht_rc.c
KERN_SRCS += smac/ap.c
KERN_SRCS += smac/ampdu.c
KERN_SRCS += smac/ssv6xxx_debugfs.c
KERN_SRCS += smac/sec_ccmp.c
KERN_SRCS += smac/sec_tkip.c
KERN_SRCS += smac/sec_wep.c
KERN_SRCS += smac/wapi_sms4.c
KERN_SRCS += smac/sec_wpi.c
KERN_SRCS += smac/efuse.c
KERN_SRCS += smac/ssv_pm.c
KERN_SRCS += smac/sar.c
KERN_SRCS += smac/ssv_cfgvendor.c


ifeq ($(findstring -DCONFIG_SSV_SMARTLINK, $(ccflags-y)), -DCONFIG_SSV_SMARTLINK)
KERN_SRCS += smac/smartlink.c
endif

KERN_SRCS += hwif/sdio/sdio.c
#KERNEL_MODULES += crypto

ifeq ($(findstring -DCONFIG_SSV_SUPPORT_AES_ASM, $(ccflags-y)), -DCONFIG_SSV_SUPPORT_AES_ASM)
KERN_SRCS += crypto/aes_glue.c
KERN_SRCS += crypto/sha1_glue.c
KERN_SRCS_S := crypto/aes-armv4.S
KERN_SRCS_S += crypto/sha1-armv4-large.S
endif

KERN_SRCS += ssv6051-generic-wlan.c

$(KMODULE_NAME)-y += $(KERN_SRCS_S:.S=.o)
$(KMODULE_NAME)-y += $(KERN_SRCS:.c=.o)

obj-$(CONFIG_SSV6200_CORE) += $(KMODULE_NAME).o

$(shell cp  $(KBUILD_TOP)/firmware/ssv6051-wifi.cfg $(FIRMWARE_PATH)/)
$(shell cp  $(KBUILD_TOP)/firmware/ssv6051-sw.bin $(FIRMWARE_PATH)/)
#$(shell cp  $(KBUILD_TOP)/ssv6051.ko $(KO_PATH)/ssv6051.ko)



