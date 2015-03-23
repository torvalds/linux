SHELL := /bin/bash
EXTRA_CFLAGS += $(USER_EXTRA_CFLAGS)
EXTRA_CFLAGS += -O1

EXTRA_CFLAGS += -Wno-unused-variable
EXTRA_CFLAGS += -Wno-unused-value
EXTRA_CFLAGS += -Wno-unused-label
EXTRA_CFLAGS += -Wno-unused-parameter
EXTRA_CFLAGS += -Wno-unused-function
EXTRA_CFLAGS += -Wno-unused

EXTRA_CFLAGS += -Wno-uninitialized

EXTRA_CFLAGS += -I$(src)/include

ccflags-y += -D__CHECK_ENDIAN__

CONFIG_AUTOCFG_CP = n

CONFIG_RTL8188E = y

CONFIG_USB_HCI = y

CONFIG_BT_COEXIST = n
CONFIG_WOWLAN = n

export TopDIR ?= $(shell pwd)


OUTSRC_FILES :=				\
		hal/HalHWImg8188E_MAC.o	\
		hal/HalHWImg8188E_BB.o	\
		hal/HalHWImg8188E_RF.o	\
		hal/HalPhyRf.o		\
		hal/HalPhyRf_8188e.o	\
		hal/HalPwrSeqCmd.o	\
		hal/Hal8188EPwrSeq.o	\
		hal/Hal8188ERateAdaptive.o\
		hal/hal_intf.o		\
		hal/hal_com.o		\
		hal/odm.o		\
		hal/odm_debug.o		\
		hal/odm_interface.o	\
		hal/odm_HWConfig.o	\
		hal/odm_RegConfig8188E.o\
		hal/odm_RTL8188E.o	\
		hal/rtl8188e_cmd.o	\
		hal/rtl8188e_dm.o	\
		hal/rtl8188e_hal_init.o	\
		hal/rtl8188e_mp.o	\
		hal/rtl8188e_phycfg.o	\
		hal/rtl8188e_rf6052.o	\
		hal/rtl8188e_rxdesc.o	\
		hal/rtl8188e_sreset.o	\
		hal/rtl8188e_xmit.o	\
		hal/rtl8188eu_led.o	\
		hal/rtl8188eu_recv.o	\
		hal/rtl8188eu_xmit.o	\
		hal/usb_halinit.o	\
		hal/usb_ops_linux.o

RTL871X = rtl8188e

HCI_NAME = usb

_OS_INTFS_FILES :=				\
			os_dep/ioctl_linux.o	\
			os_dep/mlme_linux.o	\
			os_dep/os_intfs.o	\
			os_dep/osdep_service.o	\
			os_dep/recv_linux.o	\
			os_dep/rtw_android.o	\
			os_dep/usb_intf.o	\
			os_dep/usb_ops_linux.o	\
			os_dep/xmit_linux.o




_HAL_INTFS_FILES += $(OUTSRC_FILES)


ifeq ($(CONFIG_AUTOCFG_CP), y)

$(shell cp $(TopDIR)/autoconf_rtl8188e_usb_linux.h $(TopDIR)/include/autoconf.h)
endif

ifeq ($(CONFIG_BT_COEXIST), y)
EXTRA_CFLAGS += -DCONFIG_BT_COEXIST
endif

ifeq ($(CONFIG_WOWLAN), y)
EXTRA_CFLAGS += -DCONFIG_WOWLAN
endif

SUBARCH := $(shell uname -m | sed -e s/i.86/i386/ | sed -e s/ppc/powerpc/ | sed -e s/armv.l/arm/)

ARCH ?= $(SUBARCH)
CROSS_COMPILE ?=
KVER  := $(shell uname -r)
KSRC ?= /lib/modules/$(KVER)/build
MODDESTDIR := /lib/modules/$(KVER)/kernel/drivers/net/wireless
INSTALL_PREFIX :=

ifneq ($(KERNELRELEASE),)

rtk_core :=				\
		core/rtw_ap.o		\
		core/rtw_br_ext.o	\
		core/rtw_cmd.o		\
		core/rtw_debug.o	\
		core/rtw_efuse.o	\
		core/rtw_ieee80211.o	\
		core/rtw_io.o		\
		core/rtw_ioctl_set.o	\
		core/rtw_iol.o		\
		core/rtw_led.o		\
		core/rtw_mlme.o		\
		core/rtw_mlme_ext.o	\
		core/rtw_mp.o		\
		core/rtw_mp_ioctl.o	\
		core/rtw_pwrctrl.o	\
		core/rtw_p2p.o		\
		core/rtw_recv.o		\
		core/rtw_rf.o		\
		core/rtw_security.o	\
		core/rtw_sreset.o	\
		core/rtw_sta_mgt.o	\
		core/rtw_wlan_util.o	\
		core/rtw_xmit.o	

8188eu-y += $(rtk_core)

8188eu-y += $(_HAL_INTFS_FILES)

8188eu-y += $(_OS_INTFS_FILES)

obj-$(CONFIG_RTL8188EU) := 8188eu.o

else

export CONFIG_RTL8188EU = m

all: modules

modules:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KSRC) M=$(shell pwd)  modules

strip:
	$(CROSS_COMPILE)strip 8188eu.ko --strip-unneeded

install:
	install -p -m 644 8188eu.ko  $(MODDESTDIR)
	@if [ -a /lib/modules/$(KVER)/kernel/drivers/staging/rtl8188eu/r8188eu.ko ] ; then modprobe -r r8188eu; fi;
	@echo "blacklist r8188eu" > /etc/modprobe.d/50-8188eu.conf
	cp rtl8188eufw.bin /lib/firmware/.
	/sbin/depmod -a ${KVER}
	mkdir -p /lib/firmware/rtlwifi
	cp -n rtl8188eufw.bin /lib/firmware/rtlwifi/.

uninstall:
	rm -f $(MODDESTDIR)/8188eu.ko
	/sbin/depmod -a ${KVER}
	@rm /etc/modprobe.d/50-8188eu.conf

config_r:
	@echo "make config"
	/bin/bash script/Configure script/config.in

.PHONY: modules clean clean_odm-8192c

clean_odm-8192c:
	cd hal/OUTSRC/rtl8192c ; rm -fr *.mod.c *.mod *.o .*.cmd *.ko

clean: $(clean_more)
	rm -fr *.mod.c *.mod *.o .*.cmd *.ko *~
	rm -fr .tmp_versions
	rm -fr Module.symvers ; rm -fr Module.markers ; rm -fr modules.order
	cd core ; rm -fr *.mod.c *.mod *.o .*.cmd *.ko
	cd hal ; rm -fr *.mod.c *.mod *.o .*.cmd *.ko
	cd os_dep ; rm -fr *.mod.c *.mod *.o .*.cmd *.ko
endif

