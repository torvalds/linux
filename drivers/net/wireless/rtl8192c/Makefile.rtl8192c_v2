#
# Makefile for RTL8188/8192 USB WiFi
#

EXTRA_CFLAGS += -O1 
#EXTRA_CFLAGS += -O3
EXTRA_CFLAGS += -Wall 
#EXTRA_CFLAGS += -Wextra 
#EXTRA_CFLAGS += -Werror
#EXTRA_CFLAGS += -pedantic
#EXTRA_CFLAGS += -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes 

EXTRA_CFLAGS += -Wno-unused-variable 
EXTRA_CFLAGS += -Wno-unused-value 
EXTRA_CFLAGS += -Wno-unused-label 
EXTRA_CFLAGS += -Wno-unused-parameter
EXTRA_CFLAGS += -Wno-unused-function
EXTRA_CFLAGS += -Wno-unused

EXTRA_CFLAGS += -Wno-uninitialized

EXTRA_CFLAGS += -I$(src)/include

CONFIG_AUTOCFG_CP = n

CONFIG_RTL8192C = y
CONFIG_RTL8192D = n

CONFIG_USB_HCI = y
CONFIG_PCI_HCI = n
CONFIG_SDIO_HCI = n

CONFIG_MP_INCLUDED = n
CONFIG_POWER_SAVING			=	n
CONFIG_USB_AUTOSUSPEND			=	n
CONFIG_HW_PWRP_DETECTION		=	n
CONFIG_WIFI_TEST			=	n
CONFIG_BT_COEXISTENCE			= 	n
CONFIG_RTL8192CU_REDEFINE_1X1 		=	n

CONFIG_PLATFORM_ARM_RK2918	=	y

CONFIG_DRVEXT_MODULE = n

export TopDIR ?= $(shell pwd)


ifeq ($(CONFIG_RTL8192C), y)

RTL871X = rtl8192c

ifeq ($(CONFIG_USB_HCI), y)
MODULE_NAME = 8192cu
FW_FILES := hal/$(RTL871X)/usb/Hal8192CUHWImg.o
endif

CHIP_FILES := hal/$(RTL871X)/$(RTL871X)_sreset.o
CHIP_FILES += $(FW_FILES)
endif

os_dep/linux/ioctl_linux.o: os_dep/linux/ioctl_linux.uu
	@echo "UUDE    os_dep/linux/ioctl_linux.uu"
	@uudecode os_dep/linux/ioctl_linux.uu -o os_dep/linux/ioctl_linux.o

ifeq ($(CONFIG_USB_HCI), y)

HCI_NAME = usb

_OS_INTFS_FILES :=	os_dep/osdep_service.o \
			os_dep/linux/os_intfs.o \
			os_dep/linux/$(HCI_NAME)_intf.o \
			os_dep/linux/ioctl_linux.o \
			os_dep/linux/xmit_linux.o \
			os_dep/linux/ioctl_linux.o \
			os_dep/linux/mlme_linux.o \
			os_dep/linux/recv_linux.o 

_HAL_INTFS_FILES :=	hal/hal_init.o \
            hal/$(RTL871X)/$(RTL871X)_hal_init.o \
			hal/$(RTL871X)/$(RTL871X)_phycfg.o \
			hal/$(RTL871X)/$(RTL871X)_rf6052.o \
			hal/$(RTL871X)/$(RTL871X)_dm.o \
			hal/$(RTL871X)/$(RTL871X)_rxdesc.o \
			hal/$(RTL871X)/$(RTL871X)_cmd.o \
			hal/$(RTL871X)/usb/usb_ops_linux.o \
			hal/$(RTL871X)/usb/usb_halinit.o \
			hal/$(RTL871X)/usb/rtl$(MODULE_NAME)_led.o \
			hal/$(RTL871X)/usb/rtl$(MODULE_NAME)_xmit.o \
			hal/$(RTL871X)/usb/rtl$(MODULE_NAME)_recv.o
			
_HAL_INTFS_FILES += $(CHIP_FILES)

endif

ifeq ($(CONFIG_AUTOCFG_CP), y)
$(shell cp $(TopDIR)/autoconf_$(RTL871X)_$(HCI_NAME)_linux.h $(TopDIR)/include/autoconf.h)
endif


ifeq ($(CONFIG_USB_HCI), y)
ifeq ($(CONFIG_USB_AUTOSUSPEND), y)
EXTRA_CFLAGS += -DCONFIG_USB_AUTOSUSPEND
endif
endif

ifeq ($(CONFIG_POWER_SAVING), y)
EXTRA_CFLAGS += -DCONFIG_POWER_SAVING
endif

ifeq ($(CONFIG_HW_PWRP_DETECTION), y)
EXTRA_CFLAGS += -DCONFIG_HW_PWRP_DETECTION
endif

ifeq ($(CONFIG_WIFI_TEST), y)
EXTRA_CFLAGS += -DCONFIG_WIFI_TEST
endif

ifeq ($(CONFIG_BT_COEXISTENCE), y)
EXTRA_CFLAGS += -DCONFIG_BT_COEXISTENCE
endif 

ifeq ($(CONFIG_RTL8192CU_REDEFINE_1X1), y)
EXTRA_CFLAGS += -DRTL8192C_RECONFIG_TO_1T1R
endif 

ifeq ($(CONFIG_PLATFORM_ARM_RK2918), y)
EXTRA_CFLAGS += -DCONFIG_LITTLE_ENDIAN -DCONFIG_PLATFORM_ANDROID -DCONFIG_PLATFORM_ROCKCHIPS
ARCH := arm
CROSS_COMPILE := /opt/toolchain/arm-eabi-4.4.0/bin/arm-eabi-
KSRC := 2.6.25.1
#MODULE_NAME := wlan0
endif

#
# Add minimize memory usage.
#
EXTRA_CFLAGS += -DCONFIG_MINIMAL_MEMORY_USAGE

ifneq ($(KERNELRELEASE),)


rtk_core :=	core/rtw_cmd.o \
		core/rtw_security.o \
		core/rtw_debug.o \
		core/rtw_io.o \
		core/rtw_ioctl_query.o \
		core/rtw_ioctl_set.o \
		core/rtw_ieee80211.o \
		core/rtw_mlme.o \
		core/rtw_mlme_ext.o \
		core/rtw_wlan_util.o \
		core/rtw_pwrctrl.o \
		core/rtw_rf.o \
		core/rtw_recv.o \
		core/rtw_sta_mgt.o \
		core/rtw_xmit.o	\
		core/rtw_p2p.o

rtl$(MODULE_NAME)-y += $(rtk_core)
									
rtl$(MODULE_NAME)-y += core/efuse/rtw_efuse.o

rtl$(MODULE_NAME)-y += $(_HAL_INTFS_FILES)

rtl$(MODULE_NAME)-y += $(_OS_INTFS_FILES)

core/rtw_ioctl_set.o: core/rtw_ioctl_set.uu
	@echo "UUDE    core/rtw_ioctl_set.uu"
	@uudecode core/rtw_ioctl_set.uu -o core/rtw_ioctl_set.o

rtl$(MODULE_NAME)-$(CONFIG_MP_INCLUDED) += core/rtw_mp.o \
					core/rtw_mp_ioctl.o \
					core/rtw_ioctl_rtl.o

rk_cfg := \
			os_dep/linux/wifi_power.o \
            os_dep/linux/wifi_power_usb.o \
            os_dep/linux/wifi_power_ops.o 

rkcfg-y += $(rk_cfg)
obj-$(CONFIG_RTL8192CU) := rtl$(MODULE_NAME).o rkcfg.o

else

export CONFIG_RTL8192CU = m

all: modules

modules:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KSRC) M=$(shell pwd)  modules

strip:
	$(CROSS_COMPILE)strip $(MODULE_NAME).ko --strip-unneeded

config_r:
	@echo "make config"
	/bin/bash script/Configure script/config.in
	
.PHONY: modules clean

endif

