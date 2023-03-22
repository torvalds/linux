#EXTRA_CFLAGS += $(USER_EXTRA_CFLAGS)
#EXTRA_CFLAGS += -Wno-implicit-fallthrough

RWNX_VERS_NUM := 6.4.3.0

CONFIG_AIC8800_WLAN_SUPPORT = m
MODULE_NAME = aic8800_fdrv
CONFIG_COUNTRY_CODE = "00"

# Support of bootrom start
CONFIG_START_FROM_BOOTROM = y

# Support of pmic setting, new version bootrom avaliable
CONFIG_PMIC_SETTING ?=n

# Select 8800DC/DW DCDC_VRF mode, check your board
CONFIG_VRF_DCDC_MODE = y

# ROM patch enabled option
CONFIG_ROM_PATCH_EN ?=y

#
# WAITING FOR KCONFIG {
#
CONFIG_RWNX_SOFTMAC ?= n
CONFIG_RWNX_FULLMAC ?= y
CONFIG_RWNX_FHOST ?= n

#
# DEBUG OPTIONS
CONFIG_RWNX_UM_HELPER_DFLT ?= "/dini/dini_bin/rwnx_umh.sh"
CONFIG_AIC_FW_PATH = "/vendor/etc/firmware"
export CONFIG_AIC_FW_PATH

#
# FW ARCH:
CONFIG_RWNX_SDM ?= n
CONFIG_RWNX_TL4 ?= n

# IPC version
CONFIG_RWNX_OLD_IPC ?= n

# Support of P2P DebugFS for enabling/disabling NoA and OppPS
CONFIG_RWNX_P2P_DEBUGFS ?= y

#
# } // WAITING FOR KCONFIG
#

# Enable A-MSDU support (need FW support)
## Select this if FW is compiled with AMSDU support
CONFIG_RWNX_SPLIT_TX_BUF ?= n
## Select this TO send AMSDU
CONFIG_RWNX_AMSDUS_TX ?= n

# Enable BFMER support (need FW support)
CONFIG_RWNX_BFMER ?= n

CONFIG_SDIO_SUPPORT =n
CONFIG_USB_SUPPORT =y
CONFIG_RX_REORDER ?=y
CONFIG_ARP_OFFLOAD =y
CONFIG_USE_5G ?= y
CONFIG_RADAR_OR_IR_DETECT =n
CONFIG_DOWNLOAD_FW =n
CONFIG_RFTEST=y
CONFIG_USB_BT=y
CONFIG_MAC_RANDOM_IF_NO_MAC_IN_EFUSE = y
CONFIG_WPA3_FOR_OLD_KERNEL ?= n
CONFIG_HE_FOR_OLD_KERNEL ?= n
CONFIG_VHT_FOR_OLD_KERNEL ?= n
# CONFIG_COEX = n for BT_ONLY, CONFIG_COEX =y for combo and sw
CONFIG_COEX = y
CONFIG_ALIGN_8BYTES = n
CONFIG_TXRX_THREAD_PRIO = n
CONFIG_USB_ALIGN_DATA = n
CONFIG_RX_TASKLET = n
CONFIG_TX_TASKLET = n
CONFIG_RX_NETIF_RECV_SKB = n
CONFIG_BR_SUPPORT = n
CONFIG_USB_MSG_OUT_EP = y
CONFIG_USB_MSG_IN_EP = y

#DCDW support tx aggr, D80 support both
CONFIG_USB_RX_AGGR = n
CONFIG_USB_TX_AGGR = n

CONFIG_USB_NO_TRANS_DMA_MAP = n
CONFIG_GPIO_WAKEUP = n
CONFIG_CREATE_TRACE_POINTS = n
CONFIG_SUPPORT_REALTIME_CHANGE_MAC = y
CONFIG_USE_USB_ZERO_PACKET = y
# CONFIG_DEBUG_FS = n
CONFIG_STA_SCAN_WHEN_P2P_WORKING = y
CONFIG_SET_VENDOR_EXTENSION_IE = n
CONFIG_VENDOR_GPIO = n
CONFIG_FWLOG_EN = n
CONFIG_FOR_IPCAM = n
# Need to set fw path in BOARD_KERNEL_CMDLINE
CONFIG_USE_FW_REQUEST = n
CONFIG_USE_P2P0 = n
CONFIG_ONE_TXQ = n
CONFIG_PER_STA_FC = n
CONFIG_PREALLOC_RX_SKB = n
CONFIG_PREALLOC_TXQ = y
CONFIG_USE_WIRELESS_EXT = n

# Support of MU-MIMO transmission (need FW support)
ifeq ($(CONFIG_RWNX_BFMER), y)
CONFIG_RWNX_MUMIMO_TX ?= n
else
CONFIG_RWNX_MUMIMO_TX = n
endif

# Enable handling of radar event
CONFIG_RWNX_RADAR ?= y

# Enable HW queue for Broadcast/Multicast traffic (need FW support)
CONFIG_RWNX_BCMC ?= y

# Enable Monitor+Data interface support (need FW support)
CONFIG_RWNX_MON_DATA =n
CONFIG_RWNX_MON_XMIT ?= n
CONFIG_RWNX_MON_RXFILTER ?= n

# extra DEBUG config
CONFIG_RWNX_SW_PROFILING ?= n
CONFIG_RWNX_DBG ?= y
BR_NAME = br0

obj-$(CONFIG_AIC8800_WLAN_SUPPORT) := $(MODULE_NAME).o
$(MODULE_NAME)-y := \
	rwnx_msg_tx.o          \
	rwnx_msg_rx.o          \
	rwnx_utils.o           \
	rwnx_cmds.o            \
	rwnx_irqs.o            \
	rwnx_cfgfile.o         \
	rwnx_strs.o            \
	rwnx_rx.o              \
	rwnx_tx.o              \
	rwnx_txq.o             \
	rwnx_main.o            \
	rwnx_mod_params.o      \
	rwnx_mesh.o            \
	rwnx_platform.o        \
	rwnx_pci.o             \
	rwnx_dini.o            \
	rwnx_v7.o              \
	ipc_host.o             \
	rwnx_tdls.o            \
	regdb.o                \
	md5.o                  \
	aicwf_compat_8800dc.o  \

$(MODULE_NAME)-$(CONFIG_BR_SUPPORT)       += aic_br_ext.o
$(MODULE_NAME)-$(CONFIG_RWNX_RADAR)       += rwnx_radar.o
$(MODULE_NAME)-$(CONFIG_DEBUG_FS)         += rwnx_debugfs.o
$(MODULE_NAME)-$(CONFIG_DEBUG_FS)         += rwnx_fw_trace.o
$(MODULE_NAME)-$(CONFIG_NL80211_TESTMODE) += rwnx_testmode.o
$(MODULE_NAME)-$(CONFIG_RWNX_BFMER)       += rwnx_bfmer.o
$(MODULE_NAME)-$(CONFIG_RWNX_MUMIMO_TX)   += rwnx_mu_group.o
$(MODULE_NAME)-$(CONFIG_SDIO_SUPPORT)     += sdio_host.o
$(MODULE_NAME)-$(CONFIG_SDIO_SUPPORT)     += aicwf_txrxif.o
$(MODULE_NAME)-$(CONFIG_SDIO_SUPPORT)     += aicwf_sdio.o

$(MODULE_NAME)-$(CONFIG_USB_SUPPORT)     += usb_host.o
$(MODULE_NAME)-$(CONFIG_USB_SUPPORT)     += aicwf_txrxif.o
$(MODULE_NAME)-$(CONFIG_USB_SUPPORT)     += aicwf_usb.o
$(MODULE_NAME)-$(CONFIG_USE_WIRELESS_EXT)	 += aicwf_wext_linux.o


ccflags-$(CONFIG_DEBUG_FS) += -DCONFIG_RWNX_DEBUGFS
ccflags-$(CONFIG_DEBUG_FS) += -DCONFIG_RWNX_UM_HELPER_DFLT=\"$(CONFIG_RWNX_UM_HELPER_DFLT)\"
ccflags-$(CONFIG_RWNX_P2P_DEBUGFS) += -DCONFIG_RWNX_P2P_DEBUGFS

# FW VARS
ccflags-y += -DNX_VIRT_DEV_MAX=4

#for 8800D and DCDW u01
#ccflags-y += -DNX_REMOTE_STA_MAX=8

#for 8800DCDW u02
ccflags-y += -DNX_REMOTE_STA_MAX_FOR_OLD_IC=8
ccflags-y += -DNX_REMOTE_STA_MAX=32

ccflags-y += -DNX_MU_GROUP_MAX=62
ccflags-y += -DNX_TXDESC_CNT=64
ccflags-y += -DNX_TX_MAX_RATES=4
ccflags-y += -DNX_CHAN_CTXT_CNT=3

# FW ARCH:
ccflags-$(CONFIG_RWNX_SDM) += -DCONFIG_RWNX_SDM
ccflags-$(CONFIG_RWNX_TL4) += -DCONFIG_RWNX_TL4
ccflags-$(CONFIG_RWNX_OLD_IPC) += -DCONFIG_RWNX_OLD_IPC
ccflags-$(CONFIG_START_FROM_BOOTROM) += -DCONFIG_START_FROM_BOOTROM
ccflags-$(CONFIG_PMIC_SETTING) += -DCONFIG_PMIC_SETTING
ccflags-$(CONFIG_VRF_DCDC_MODE) += -DCONFIG_VRF_DCDC_MODE
ccflags-$(CONFIG_ROM_PATCH_EN) += -DCONFIG_ROM_PATCH_EN
ccflags-$(CONFIG_WPA3_FOR_OLD_KERNEL) += -DCONFIG_WPA3_FOR_OLD_KERNEL
ccflags-$(CONFIG_HE_FOR_OLD_KERNEL) += -DCONFIG_HE_FOR_OLD_KERNEL
ccflags-$(CONFIG_VHT_FOR_OLD_KERNEL) += -DCONFIG_VHT_FOR_OLD_KERNEL
ccflags-$(CONFIG_COEX) += -DCONFIG_COEX

ccflags-y += -DCONFIG_RWNX_FULLMAC
ccflags-y += -I$(src)/.
ccflags-$(CONFIG_RWNX_RADAR) += -DCONFIG_RWNX_RADAR
ccflags-$(CONFIG_RWNX_MON_DATA) += -DCONFIG_RWNX_MON_DATA
ccflags-$(CONFIG_RWNX_MON_XMIT) += -DCONFIG_RWNX_MON_XMIT
ccflags-$(CONFIG_RWNX_MON_RXFILTER) += -DCONFIG_RWNX_MON_RXFILTER
ccflags-$(CONFIG_RWNX_BFMER) += -DCONFIG_RWNX_BFMER
ccflags-$(CONFIG_RWNX_SPLIT_TX_BUF) += -DCONFIG_RWNX_SPLIT_TX_BUF
ifeq ($(CONFIG_RWNX_SPLIT_TX_BUF), y)
ccflags-$(CONFIG_RWNX_AMSDUS_TX) += -DCONFIG_RWNX_AMSDUS_TX
endif
ccflags-$(CONFIG_RWNX_DBG) += -DCONFIG_RWNX_DBG
ccflags-$(CONFIG_RWNX_SW_PROFILING) += -DCONFIG_RWNX_SW_PROFILING
ccflags-$(CONFIG_RWNX_MUMIMO_TX) += -DCONFIG_RWNX_MUMIMO_TX
ccflags-$(CONFIG_RFTEST) += -DCONFIG_RFTEST


ifeq ($(CONFIG_SDIO_SUPPORT), y)
ccflags-y += -DAICWF_SDIO_SUPPORT
endif

ifeq ($(CONFIG_USB_SUPPORT), y)
ccflags-y += -DAICWF_USB_SUPPORT
endif
ifeq ($(CONFIG_BR_SUPPORT), y)
ccflags-y += -DCONFIG_BR_SUPPORT
ccflags-y += '-DCONFIG_BR_SUPPORT_BRNAME="'$(BR_NAME)'"'
endif

ifeq ($(CONFIG_RWNX_MUMIMO_TX), y)
ccflags-y += -DCONFIG_USER_MAX=2
else
ccflags-y += -DCONFIG_USER_MAX=1
endif

ifeq ($(CONFIG_RWNX_BCMC), y)
ccflags-y += -DNX_TXQ_CNT=5
else
ccflags-y += -DNX_TXQ_CNT=4
endif

# For old kernel (<=3.19)
ifeq ($(shell test $(VERSION) -lt 4 -a "$(CONFIG_VENDOR_RWNX)" = y ; echo $$?),0)
ccflags-y += -DCONFIG_VENDOR_RWNX_VHT_NO80
endif

ccflags-$(CONFIG_RX_REORDER) += -DAICWF_RX_REORDER
ccflags-$(CONFIG_ARP_OFFLOAD) += -DAICWF_ARP_OFFLOAD
ccflags-$(CONFIG_USE_5G) += -DUSE_5G
ccflags-$(CONFIG_RADAR_OR_IR_DETECT) += -DRADAR_OR_IR_DETECT
ccflags-$(CONFIG_DOWNLOAD_FW)  += -DCONFIG_DOWNLOAD_FW
ccflags-$(CONFIG_USB_BT)  += -DCONFIG_USB_BT
ccflags-$(CONFIG_ALIGN_8BYTES)  += -DCONFIG_ALIGN_8BYTES
ccflags-$(CONFIG_TXRX_THREAD_PRIO) += -DCONFIG_TXRX_THREAD_PRIO
ccflags-$(CONFIG_USB_ALIGN_DATA) += -DCONFIG_USB_ALIGN_DATA
ccflags-$(CONFIG_MAC_RANDOM_IF_NO_MAC_IN_EFUSE) += -DCONFIG_MAC_RANDOM_IF_NO_MAC_IN_EFUSE
ccflags-$(CONFIG_VENDOR_GPIO) += -DCONFIG_VENDOR_GPIO
ccflags-y += -DDEFAULT_COUNTRY_CODE=""\$(CONFIG_COUNTRY_CODE)"\"
ccflags-$(CONFIG_RX_NETIF_RECV_SKB) += -DCONFIG_RX_NETIF_RECV_SKB
ccflags-$(CONFIG_USB_MSG_OUT_EP) += -DCONFIG_USB_MSG_OUT_EP
ccflags-$(CONFIG_USB_MSG_IN_EP) += -DCONFIG_USB_MSG_IN_EP
ccflags-$(CONFIG_USB_RX_AGGR)  += -DCONFIG_USB_RX_AGGR
ccflags-$(CONFIG_USB_TX_AGGR) += -DCONFIG_USB_TX_AGGR
ccflags-$(CONFIG_USB_NO_TRANS_DMA_MAP) += -DCONFIG_USB_NO_TRANS_DMA_MAP
ccflags-$(CONFIG_GPIO_WAKEUP) += -DCONFIG_GPIO_WAKEUP
ccflags-$(CONFIG_CREATE_TRACE_POINTS) += -DCREATE_TRACE_POINTS
ccflags-$(CONFIG_RX_TASKLET) += -DCONFIG_RX_TASKLET
ccflags-$(CONFIG_TX_TASKLET) += -DCONFIG_TX_TASKLET
ccflags-$(CONFIG_USE_USB_ZERO_PACKET) += -DCONFIG_USE_USB_ZERO_PACKET
ccflags-$(CONFIG_STA_SCAN_WHEN_P2P_WORKING) += -DCONFIG_STA_SCAN_WHEN_P2P_WORKING
ccflags-$(CONFIG_SUPPORT_REALTIME_CHANGE_MAC) += -DCONFIG_SUPPORT_REALTIME_CHANGE_MAC
ccflags-$(CONFIG_SET_VENDOR_EXTENSION_IE) += -DCONFIG_SET_VENDOR_EXTENSION_IE
ccflags-$(CONFIG_FWLOG_EN) += -DCONFIG_FWLOG_EN
ccflags-$(CONFIG_FOR_IPCAM) += -DCONFIG_FOR_IPCAM
ccflags-$(CONFIG_USE_FW_REQUEST) += -DCONFIG_USE_FW_REQUEST
ccflags-$(CONFIG_USE_P2P0) += -DCONFIG_USE_P2P0
ccflags-$(CONFIG_ONE_TXQ)  += -DCONFIG_ONE_TXQ
ccflags-$(CONFIG_PER_STA_FC)  += -DCONFIG_PER_STA_FC
ccflags-$(CONFIG_PREALLOC_RX_SKB) += -DCONFIG_PREALLOC_RX_SKB
ccflags-$(CONFIG_PREALLOC_TXQ) += -DCONFIG_PREALLOC_TXQ
ccflags-$(CONFIG_USE_WIRELESS_EXT) += -DCONFIG_USE_WIRELESS_EXT

# Platform support list
CONFIG_PLATFORM_ROCKCHIP ?= n
CONFIG_PLATFORM_ALLWINNER ?= n
CONFIG_PLATFORM_AMLOGIC ?= n
CONFIG_PLATFORM_UBUNTU ?= y

ifeq ($(CONFIG_PLATFORM_ROCKCHIP), y)
#KDIR ?= /home/yaya/E/Rockchip/3288/Android5/kernel/
#ARCH ?= arm
#CROSS_COMPILE ?= /home/yaya/D/Workspace/CyberQuantum/JinHaoYue/rk3288/prebuilts/gcc/linux-x86/arm/arm-eabi-4.6/bin/arm-eabi-
#KDIR ?= /home/yaya/E/Rockchip/3229/Android7/RK3229_ANDROID7.1_v1.01_20170914/rk3229_Android7.1_v1.01_xml0914/kernel
#ARCH ?= arm
#CROSS_COMPILE ?= /home/yaya/E/Rockchip/3229/Android7/RK3229_ANDROID7.1_v1.01_20170914/rk3229_Android7.1_v1.01_xml0914/prebuilts/gcc/linux-x86/arm/arm-eabi-4.6/bin/arm-eabi-
#KDIR  ?= /home/yaya/E/Rockchip/3229/Android9/rk3229_android9.0_box/kernel
#ARCH ?= arm
#CROSS_COMPILE ?= /home/yaya/E/Rockchip/3229/Android9/rk3229_android9.0_box/prebuilts/gcc/linux-x86/arm/gcc-linaro-6.3.1-2017.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-
KDIR ?= /home/yaya/E/Rockchip/3566/firefly/Android11.0/Firefly-RK356X_Android11.0_git_20210824/RK356X_Android11.0/kernel
ARCH ?= arm64
CROSS_COMPILE ?= /home/yaya/E/Rockchip/3566/Android11/rk3566_rk3568_android11_oranth/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-
#ARCH ?= arm
#CROSS_COMPILE ?=/home/yaya/D/Workspace/CyberQuantum/JinHaoYue/rk3288/prebuilts/gcc/linux-x86/arm/arm-eabi-4.6/bin/arm-eabi-
#KDIR ?=/home/yaya/D/Workspace/CyberQuantum/JinHaoYue/rk3288/kernel
#KDIR := /home/yaya/E/Rockchip/3566/Android/kernel
#ARCH ?= arm64
#CROSS_COMPILE ?= /home/yaya/E/Rockchip/3566/Android/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-
#KDIR ?= /home/yaya/E/Rockchip/3566/Android11/rk3566_rk3568_android11_oranth/kernel
#ARCH ?= arm64
#CROSS_COMPILE ?= /home/yaya/E/Rockchip/3566/Android11/rk3566_rk3568_android11_oranth/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-
#KDIR ?= /home/yaya/D/samba1/aiden/SDK/Rockchip/3588/Android12/RK3588_Android12.0/kernel-5.10
#ARCH ?= arm64
#CROSS_COMPILE ?= /home/yaya/D/samba1/aiden/SDK/Rockchip/3588/Android12/RK3588_Android12.0/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-
ccflags-y += -DANDROID_PLATFORM
ccflags-y += -DCONFIG_PLATFORM_ROCKCHIP
endif

ifeq ($(CONFIG_PLATFORM_ALLWINNER), y)
KDIR  ?= /home/yaya/E/Allwinner/R818/R818/AndroidQ/lichee/kernel/linux-4.9
ARCH ?= arm64
CROSS_COMPILE ?= /home/yaya/E/Allwinner/R818/R818/AndroidQ/lichee/out/gcc-linaro-5.3.1-2016.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-
ccflags-y += -DANDROID_PLATFORM
endif

ifeq ($(CONFIG_PLATFORM_AMLOGIC), y)
ccflags-y += -DANDROID_PLATFORM
ARCH ?= arm
CROSS_COMPILE ?= /home/yaya/D/Workspace/CyberQuantum/JinHaoYue/amls905x3/SDK/20191101-0tt-asop/android9.0/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9/bin/arm-linux-androidkernel-
KDIR ?= /home/yaya/D/Workspace/CyberQuantum/JinHaoYue/amls905x3/SDK/20191101-0tt-asop/android9.0/out/target/product/u202/obj/KERNEL_OBJ/

endif

ifeq ($(CONFIG_PLATFORM_UBUNTU), y)
KDIR  ?= /lib/modules/$(shell uname -r)/build
#KDIR ?= ~/D/Workspace/CyberQuantum/Linux/linux-4.15/
PWD   ?= $(shell pwd)
KVER ?= $(shell uname -r)
MODDESTDIR ?= /lib/modules/$(KVER)/kernel/drivers/net/wireless/aic8800
ARCH ?= x86_64
CROSS_COMPILE ?=
endif


all: modules
modules:
#	make -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) LLVM=1 LLVM_IAS=1 modules
	make -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules
install:
	mkdir -p $(MODDESTDIR)
	install -p -m 644 $(MODULE_NAME).ko  $(MODDESTDIR)
	/sbin/depmod -a ${KVER}

uninstall:
	rm -rfv $(MODDESTDIR)/$(MODULE_NAME).ko
	/sbin/depmod -a ${KVER}

clean:
	rm -rf *.o *.ko *.o.* *.mod.* modules.* Module.* .a* .o* .*.o.* *.mod .tmp* .cache.mk

