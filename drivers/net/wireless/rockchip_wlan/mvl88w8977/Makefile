# File: Makefile
#
# Copyright (C) 2008-2017, Marvell International Ltd.
#
# This software file (the "File") is distributed by Marvell International
# Ltd. under the terms of the GNU General Public License Version 2, June 1991
# (the "License").  You may use, redistribute and/or modify this File in
# accordance with the terms and conditions of the License, a copy of which
# is available by writing to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
# worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
#
# A copy of the GPL is available in file gpl-2.0.txt accompanying in this
# deliverables.
#
# THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
# IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
# ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
# this warranty disclaimer.

COMPATDIR=/lib/modules/$(KERNELVERSION_X86)/build/compat-wireless-3.2-rc1-1/include
CC=		$(CROSS_COMPILE)gcc -I$(COMPATDIR)
LD=		$(CROSS_COMPILE)ld
BACKUP=		/root/backup
YMD=		`date +%Y%m%d%H%M`

ifneq ($(COMPAT_VERSION_CODE),)
DRV_DIR ?= $(shell pwd)
export DRV_DIR
COMPAT_VERSION=$(shell echo $(COMPAT_VERSION_CODE) | awk -F '.' '{print $$1}')
COMPAT_PATCHLEVEL=$(shell echo $(COMPAT_VERSION_CODE) | awk -F '.' '{print $$2}')
COMPAT_SUBLEVEL=$(shell echo $(COMPAT_VERSION_CODE) | awk -F '.' '{print $$3}')
DECL_HEADER_FILE=$(DRV_DIR)/mlinux/moal_main.h
$(shell sed -i 's/COMPAT_VERSION_CODE KERNEL_VERSION.*/COMPAT_VERSION_CODE KERNEL_VERSION(\
		$(COMPAT_VERSION), $(COMPAT_PATCHLEVEL), $(COMPAT_SUBLEVEL))/g' $(DECL_HEADER_FILE))
endif

#############################################################################
# Configuration Options
#############################################################################

# Debug Option
# DEBUG LEVEL n/1/2:
# n: NO DEBUG
# 1: Only PRINTM(MMSG,...), PRINTM(MFATAL,...), ...
# 2: All PRINTM()
CONFIG_DEBUG=1

# Proc debug file
CONFIG_PROC_DEBUG=y

# Enable STA mode support
CONFIG_STA_SUPPORT=y

# Enable uAP mode support
CONFIG_UAP_SUPPORT=y

# Enable WIFIDIRECT support
CONFIG_WIFI_DIRECT_SUPPORT=y

# Enable WIFIDISPLAY support
CONFIG_WIFI_DISPLAY_SUPPORT=y

# Re-association in driver
CONFIG_REASSOCIATION=y


# Manufacturing firmware support
CONFIG_MFG_CMD_SUPPORT=y

# OpenWrt support
CONFIG_OPENWRT_SUPPORT=n

# Big-endian platform
CONFIG_BIG_ENDIAN=n


# Enable driver based authenticator
CONFIG_DRV_EMBEDDED_AUTHENTICATOR=y

# Enable driver based supplicant
CONFIG_DRV_EMBEDDED_SUPPLICANT=y

ifeq ($(CONFIG_DRV_EMBEDDED_SUPPLICANT), y)
CONFIG_EMBEDDED_SUPP_AUTH=y
else
ifeq ($(CONFIG_DRV_EMBEDDED_AUTHENTICATOR), y)
CONFIG_EMBEDDED_SUPP_AUTH=y
endif
endif

# Enable SDIO multi-port Tx aggregation
CONFIG_SDIO_MULTI_PORT_TX_AGGR=y

# Enable SDIO multi-port Rx aggregation
CONFIG_SDIO_MULTI_PORT_RX_AGGR=y

# SDIO suspend/resume
CONFIG_SDIO_SUSPEND_RESUME=y

# DFS testing support
CONFIG_DFS_TESTING_SUPPORT=y

# Multi-channel support
CONFIG_MULTI_CHAN_SUPPORT=y


CONFIG_ANDROID_KERNEL=y


#32bit app over 64bit kernel support
CONFIG_USERSPACE_32BIT_OVER_KERNEL_64BIT=n

#############################################################################
# Select Platform Tools
#############################################################################
KERNELDIR := ./../../../../../
CURRENT_DIR := $(shell pwd)
MODEXT = ko

ccflags-y += -I$(M)/mlan
ccflags-y += -DLINUX

ccflags-y += -Idrivers/net/wireless/rockchip_wlan/mvl88w8977/mlan/
ccflags-y += -Idrivers/net/wireless/rockchip_wlan/mvl88w8977/mlinux
ccflags-y += -Idrivers/net/wireless/rockchip_wlan/mvl88w8977/mlan/esa
ccflags-y += -Idrivers/net/wireless/rockchip_wlan/mvl88w8977/mlan/esa/common

ifeq ($(CONFIG_EMBEDDED_SUPP_AUTH), y)
ccflags-y += -I$(M)/mlan/esa
ccflags-y += -I$(M)/mlan/esa/common
endif

LD += -S

BINDIR = ../bin_sd8977

#############################################################################
# Compiler Flags
#############################################################################

	ccflags-y += -I$(KERNELDIR)/include
	ccflags-y += -I$(PWD)/mlan/esa
	ccflags-y += -I$(PWD)/mlan/esa/common

	ccflags-y += -DFPNUM='"68"'

ifeq ($(CONFIG_DEBUG),1)
	ccflags-y += -DDEBUG_LEVEL1
endif

ifeq ($(CONFIG_DEBUG),2)
	ccflags-y += -DDEBUG_LEVEL1
	ccflags-y += -DDEBUG_LEVEL2
	DBG=	-dbg
endif

ifeq ($(CONFIG_PROC_DEBUG),y)
	ccflags-y += -DPROC_DEBUG
	export CONFIG_PROC_DEBUG
endif

ifeq ($(CONFIG_64BIT), y)
	ccflags-y += -DMLAN_64BIT
endif

ifeq ($(CONFIG_STA_SUPPORT),y)
	ccflags-y += -DSTA_SUPPORT
ifeq ($(CONFIG_REASSOCIATION),y)
	ccflags-y += -DREASSOCIATION
endif
else
CONFIG_WIFI_DIRECT_SUPPORT=n
CONFIG_WIFI_DISPLAY_SUPPORT=n
CONFIG_STA_WEXT=n
CONFIG_STA_CFG80211=n
endif

ifeq ($(CONFIG_UAP_SUPPORT),y)
	ccflags-y += -DUAP_SUPPORT
else
CONFIG_WIFI_DIRECT_SUPPORT=n
CONFIG_WIFI_DISPLAY_SUPPORT=n
CONFIG_UAP_WEXT=n
CONFIG_UAP_CFG80211=n
endif

ifeq ($(CONFIG_WIFI_DIRECT_SUPPORT),y)
	ccflags-y += -DWIFI_DIRECT_SUPPORT
endif
ifeq ($(CONFIG_WIFI_DISPLAY_SUPPORT),y)
	ccflags-y += -DWIFI_DISPLAY_SUPPORT
endif

ifeq ($(CONFIG_MFG_CMD_SUPPORT),y)
	ccflags-y += -DMFG_CMD_SUPPORT
endif

ifeq ($(CONFIG_BIG_ENDIAN),y)
	ccflags-y += -DBIG_ENDIAN_SUPPORT
endif

ifeq ($(CONFIG_USERSPACE_32BIT_OVER_KERNEL_64BIT),y)
	ccflags-y += -DUSERSPACE_32BIT_OVER_KERNEL_64BIT
endif

ifeq ($(CONFIG_SDIO_MULTI_PORT_TX_AGGR),y)
	ccflags-y += -DSDIO_MULTI_PORT_TX_AGGR
endif

ifeq ($(CONFIG_SDIO_MULTI_PORT_RX_AGGR),y)
	ccflags-y += -DSDIO_MULTI_PORT_RX_AGGR
endif

ifeq ($(CONFIG_SDIO_SUSPEND_RESUME),y)
	ccflags-y += -DSDIO_SUSPEND_RESUME
endif

ifeq ($(CONFIG_MULTI_CHAN_SUPPORT),y)
	ccflags-y += -DMULTI_CHAN_SUPPORT
endif

ifeq ($(CONFIG_DFS_TESTING_SUPPORT),y)
	ccflags-y += -DDFS_TESTING_SUPPORT
endif


ifeq ($(CONFIG_ANDROID_KERNEL), y)
	ccflags-y += -DANDROID_KERNEL
endif

ifeq ($(CONFIG_OPENWRT_SUPPORT), y)
	ccflags-y += -DOPENWRT
endif


ifeq ($(CONFIG_T50), y)
	ccflags-y += -DT50
	ccflags-y += -DT40
	ccflags-y += -DT3T
endif

# add -Wno-packed-bitfield-compat when GCC version greater than 4.4
GCC_VERSION := $(shell echo `gcc -dumpversion | cut -f1-2 -d.` \>= 4.4 | sed -e 's/\./*100+/g' | bc )
ifeq ($(GCC_VERSION),1)
	ccflags-y += -Wno-packed-bitfield-compat
endif

#############################################################################
# Make Targets
#############################################################################

ifneq ($(KERNELRELEASE),)

ifeq ($(CONFIG_WIRELESS_EXT),y)
ifeq ($(CONFIG_WEXT_PRIV),y)
	# Enable WEXT for STA
	CONFIG_STA_WEXT=y
	# Enable WEXT for uAP
	CONFIG_UAP_WEXT=y
else
# Disable WEXT for STA
	CONFIG_STA_WEXT=n
# Disable WEXT for uAP
	CONFIG_UAP_WEXT=n
endif
endif

# Enable CFG80211 for STA
ifeq ($(CONFIG_CFG80211),y)
	CONFIG_STA_CFG80211=y
else
ifeq ($(CONFIG_CFG80211),m)
	CONFIG_STA_CFG80211=y
else
	CONFIG_STA_CFG80211=n
endif
endif

# OpenWrt
ifeq ($(CONFIG_OPENWRT_SUPPORT), y)
ifeq ($(CPTCFG_CFG80211),y)
	CONFIG_STA_CFG80211=y
else
ifeq ($(CPTCFG_CFG80211),m)
	CONFIG_STA_CFG80211=y
else
	CONFIG_STA_CFG80211=n
endif
endif
endif

# Enable CFG80211 for uAP
ifeq ($(CONFIG_CFG80211),y)
	CONFIG_UAP_CFG80211=y
else
ifeq ($(CONFIG_CFG80211),m)
	CONFIG_UAP_CFG80211=y
else
	CONFIG_UAP_CFG80211=n
endif
endif

# OpenWrt
ifeq ($(CONFIG_OPENWRT_SUPPORT), y)
ifeq ($(CPTCFG_CFG80211),y)
	CONFIG_UAP_CFG80211=y
else
ifeq ($(CPTCFG_CFG80211),m)
	CONFIG_UAP_CFG80211=y
else
	CONFIG_UAP_CFG80211=n
endif
endif
endif

ifneq ($(CONFIG_STA_SUPPORT),y)
	CONFIG_WIFI_DIRECT_SUPPORT=n
	CONFIG_WIFI_DISPLAY_SUPPORT=n
	CONFIG_STA_WEXT=n
	CONFIG_STA_CFG80211=n
endif

ifneq ($(CONFIG_UAP_SUPPORT),y)
	CONFIG_WIFI_DIRECT_SUPPORT=n
	CONFIG_WIFI_DISPLAY_SUPPORT=n
	CONFIG_UAP_WEXT=n
	CONFIG_UAP_CFG80211=n
endif

ifeq ($(CONFIG_STA_SUPPORT),y)
ifeq ($(CONFIG_STA_WEXT),y)
	ccflags-y += -DSTA_WEXT
endif
ifeq ($(CONFIG_STA_CFG80211),y)
	ccflags-y += -DSTA_CFG80211
endif
endif
ifeq ($(CONFIG_UAP_SUPPORT),y)
ifeq ($(CONFIG_UAP_WEXT),y)
	ccflags-y += -DUAP_WEXT
endif
ifeq ($(CONFIG_UAP_CFG80211),y)
	ccflags-y += -DUAP_CFG80211
endif
endif

print:
ifeq ($(CONFIG_STA_SUPPORT),y)
ifeq ($(CONFIG_STA_WEXT),n)
ifeq ($(CONFIG_STA_CFG80211),n)
	@echo "Can not build STA without WEXT or CFG80211"
	exit 2
endif
endif
endif
ifeq ($(CONFIG_UAP_SUPPORT),y)
ifeq ($(CONFIG_UAP_WEXT),n)
ifeq ($(CONFIG_UAP_CFG80211),n)
	@echo "Can not build UAP without WEXT or CFG80211"
	exit 2
endif
endif
endif


ifeq ($(CONFIG_DRV_EMBEDDED_AUTHENTICATOR), y)
    ccflags-y += -DDRV_EMBEDDED_AUTHENTICATOR
endif

ifeq ($(CONFIG_DRV_EMBEDDED_SUPPLICANT), y)
    ccflags-y += -DDRV_EMBEDDED_SUPPLICANT
endif


MOALOBJS =	mlinux/moal_main.o \
		mlinux/moal_ioctl.o \
		mlinux/moal_shim.o \
		mlinux/moal_eth_ioctl.o

MLANOBJS =	mlan/mlan_shim.o mlan/mlan_init.o \
		mlan/mlan_txrx.o \
		mlan/mlan_cmdevt.o mlan/mlan_misc.o \
		mlan/mlan_cfp.o \
		mlan/mlan_module.o

MLANOBJS += mlan/mlan_wmm.o
MLANOBJS += mlan/mlan_sdio.o
MLANOBJS += mlan/mlan_11n_aggr.o
MLANOBJS += mlan/mlan_11n_rxreorder.o
MLANOBJS += mlan/mlan_11n.o
MLANOBJS += mlan/mlan_11d.o
MLANOBJS += mlan/mlan_11h.o
ifeq ($(CONFIG_STA_SUPPORT),y)
MLANOBJS += mlan/mlan_meas.o
MLANOBJS += mlan/mlan_scan.o \
			mlan/mlan_sta_ioctl.o \
			mlan/mlan_sta_rx.o \
			mlan/mlan_sta_tx.o \
			mlan/mlan_sta_event.o \
			mlan/mlan_sta_cmd.o \
			mlan/mlan_sta_cmdresp.o \
			mlan/mlan_join.o
ifeq ($(CONFIG_STA_WEXT),y)
MOALOBJS += mlinux/moal_priv.o \
            mlinux/moal_wext.o
endif
endif
ifeq ($(CONFIG_UAP_SUPPORT),y)
MLANOBJS += mlan/mlan_uap_ioctl.o
MLANOBJS += mlan/mlan_uap_cmdevent.o
MLANOBJS += mlan/mlan_uap_txrx.o
MOALOBJS += mlinux/moal_uap.o
ifeq ($(CONFIG_UAP_WEXT),y)
MOALOBJS += mlinux/moal_uap_priv.o
MOALOBJS += mlinux/moal_uap_wext.o
endif
endif
ifeq ($(CONFIG_STA_CFG80211),y)
MOALOBJS += mlinux/moal_cfg80211.o
MOALOBJS += mlinux/moal_cfgvendor.o
MOALOBJS += mlinux/moal_sta_cfg80211.o
endif
ifeq ($(CONFIG_UAP_CFG80211),y)
MOALOBJS += mlinux/moal_cfg80211.o
MOALOBJS += mlinux/moal_cfgvendor.o
MOALOBJS += mlinux/moal_uap_cfg80211.o
endif

ifdef CONFIG_PROC_FS
MOALOBJS += mlinux/moal_proc.o
ifeq ($(CONFIG_PROC_DEBUG),y)
MOALOBJS += mlinux/moal_debug.o
endif
endif




ifeq ($(CONFIG_EMBEDDED_SUPP_AUTH), y)
MLANOBJS += mlan/esa/common/rc4.o \
			mlan/esa/common/aes_cmac_rom.o \
			mlan/esa/common/hmac_sha1.o \
			mlan/esa/common/md5.o \
			mlan/esa/common/mrvl_sha256_crypto.o \
			mlan/esa/common/hmac_md5.o \
			mlan/esa/common/crypt_new_rom.o \
			mlan/esa/common/rijndael.o \
			mlan/esa/common/sha1.o \
			mlan/esa/common/sha256.o \
			mlan/esa/common/pass_phrase.o \
			mlan/esa/common/pmkCache.o \
			mlan/esa/common/pmkCache_rom.o \
			mlan/esa/common/parser.o \
			mlan/esa/common/parser_rom.o \
			mlan/esa/keyMgmtApStaCommon.o \
			mlan/esa/hostsa_init.o \
			mlan/esa/authenticator_api.o
endif


ifeq ($(CONFIG_DRV_EMBEDDED_SUPPLICANT),y)
MLANOBJS += mlan/esa/keyMgmtSta.o \
			mlan/esa/keyMgmtSta_rom.o \
			mlan/esa/supplicant.o
endif

ifeq ($(CONFIG_DRV_EMBEDDED_AUTHENTICATOR),y)
MLANOBJS += mlan/esa/AssocAp_srv_rom.o \
			mlan/esa/keyMgmtAp_rom.o \
			mlan/esa/keyMgmtAp.o
endif

ifeq ($(CONFIG_MULTI_INTERFACE),y)
obj-m := mlan_sdio.o
mlan_sdio-objs := $(MLANOBJS)
else
obj-m := mlan.o
mlan-objs := $(MLANOBJS)
endif
MOALOBJS += mlinux/moal_sdio_mmc.o
obj-m += sd8xxx.o
sd8xxx-objs := $(MOALOBJS)

# Otherwise we were called directly from the command line; invoke the kernel build system.
else

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

endif

###############################################################

export		CC LD ccflags-y KERNELDIR

echo:

build:		echo default

	@if [ ! -d $(BINDIR) ]; then \
		mkdir $(BINDIR); \
	fi

ifeq ($(CONFIG_MULTI_INTERFACE),y)
	cp -f mlan_sdio.$(MODEXT) $(BINDIR)/mlan_sdio$(DBG).$(MODEXT)
else
	cp -f mlan.$(MODEXT) $(BINDIR)/mlan$(DBG).$(MODEXT)
endif
	cp -f sd8xxx.$(MODEXT) $(BINDIR)/sd8977$(DBG).$(MODEXT)
	cp -rpf script/sdio_mmc/* $(BINDIR)/

ifeq ($(CONFIG_STA_SUPPORT),y)
	cp -f README $(BINDIR)
	cp -f README_MLAN $(BINDIR)
ifeq ($(CONFIG_OPENWRT_SUPPORT),y)
	cp -f README_OPENWRT $(BINDIR)
endif
endif
ifeq ($(CONFIG_UAP_SUPPORT),y)
	cp -f README_UAP $(BINDIR)
endif
ifeq ($(CONFIG_WIFI_DIRECT_SUPPORT),y)
	cp -f README_WIFIDIRECT $(BINDIR)
	cp -rpf script/wifidirect $(BINDIR)
ifeq ($(CONFIG_WIFI_DISPLAY_SUPPORT),y)
	cp -rpf script/wifidisplay $(BINDIR)
endif
endif

clean:
	-find . -name "*.o" -exec rm {} \;
	-find . -name "*.ko" -exec rm {} \;
	-find . -name ".*.cmd" -exec rm {} \;
	-find . -name "*.mod.c" -exec rm {} \;
	-find . -name "Module.symvers" -exec rm {} \;
	-find . -name "Module.markers" -exec rm {} \;
	-find . -name "modules.order" -exec rm {} \;
	-find . -name ".*.dwo" -exec rm {} \;
	-find . -name "*dwo" -exec rm {} \;
	-rm -rf .tmp_versions

install: default

	cp -f mlan.$(MODEXT) $(INSTALLDIR)/mlan$(DBG).$(MODEXT)
	cp -f ../io/sdio/$(PLATFORM)/sdio.$(MODEXT) $(INSTALLDIR)
	cp -f sd8xxx.$(MODEXT) $(INSTALLDIR)/sd8977$(DBG).$(MODEXT)
	echo "sd8977 Driver Installed"

distclean:
	-find . -name "*.o" -exec rm {} \;
	-find . -name "*.orig" -exec rm {} \;
	-find . -name "*.swp" -exec rm {} \;
	-find . -name "*.*~" -exec rm {} \;
	-find . -name "*~" -exec rm {} \;
	-find . -name "*.d" -exec rm {} \;
	-find . -name "*.a" -exec rm {} \;
	-find . -name "tags" -exec rm {} \;
	-find . -name ".*" -exec rm -rf 2> /dev/null \;
	-find . -name "*.ko" -exec rm {} \;
	-find . -name ".*.cmd" -exec rm {} \;
	-find . -name "*.mod.c" -exec rm {} \;
	-find . -name ".*.dwo" -exec rm {} \;
	-find . -name "*dwo" -exec rm {} \;
	-rm -rf .tmp_versions

# End of file
