# bcmdhd
# 1. WL_IFACE_COMB_NUM_CHANNELS must be added if Android version is 4.4 with Kernel version 3.0~3.4,
#    otherwise please remove it.
MODULE_NAME = bcmdhd
#CONFIG_RKWIFI = m
CONFIG_CFG80211 = y
CONFIG_BCMDHD_OOB = y

CONFIG_BCMDHD_SDIO := y
#CONFIG_BCMDHD_PCIE := y

DHDCFLAGS = -Wall -Wstrict-prototypes -Dlinux -DBCMDRIVER -DSDTEST       \
	-DBCMDONGLEHOST -DUNRELEASEDCHIP -DBCMDMA32 -DBCMFILEIMAGE            \
	-DDHDTHREAD -DDHD_DEBUG -DSHOW_EVENTS -DBCMDBG -DGET_OTP_MAC_ENABLE   \
	-DWIFI_ACT_FRAME -DARP_OFFLOAD_SUPPORT -DSUPPORT_PM2_ONLY             \
	-DKEEP_ALIVE -DPKT_FILTER_SUPPORT -DPNO_SUPPORT -DDHDTCPACK_SUPPRESS  \
	-DDHD_DONOT_FORWARD_BCMEVENT_AS_NETWORK_PKT -DRXFRAME_THREAD          \
	-DSET_RANDOM_MAC_SOFTAP                                               \
	-DENABLE_INSMOD_NO_FW_LOAD                                            \
	-Idrivers/net/wireless/rockchip_wlan/rkwifi/bcmdhd \
	-Idrivers/net/wireless/rockchip_wlan/rkwifi/bcmdhd/include

DHDOFILES = aiutils.o siutils.o sbutils.o bcmutils.o bcmwifi_channels.o \
	dhd_linux.o dhd_linux_platdev.o dhd_linux_sched.o dhd_pno.o \
	dhd_common.o dhd_ip.o dhd_linux_wq.o dhd_custom_gpio.o \
	bcmevent.o hndpmu.o linux_osl.o wldev_common.o wl_android.o \
	hnd_pktq.o hnd_pktpool.o dhd_config.o

ifneq ($(CONFIG_BCMDHD_SDIO),)
DHDCFLAGS += \
	-DBCMSDIO -DMMC_SDIO_ABORT -DBCMLXSDMMC -DUSE_SDIOFIFO_IOVAR          \
	-DBDC -DPROP_TXSTATUS -DDHD_USE_IDLECOUNT -DBCMSDIOH_TXGLOM           \
	-DCUSTOM_SDIO_F2_BLKSIZE=128

DHDOFILES += bcmsdh.o bcmsdh_linux.o bcmsdh_sdmmc.o bcmsdh_sdmmc_linux.o \
	dhd_sdio.o dhd_cdc.o dhd_wlfc.o

ifeq ($(CONFIG_BCMDHD_OOB),y)
DHDCFLAGS += -DOOB_INTR_ONLY -DHW_OOB -DCUSTOMER_OOB
ifeq ($(CONFIG_BCMDHD_DISABLE_WOWLAN),y)
DHDCFLAGS += -DDISABLE_WOWLAN
endif
else
DHDCFLAGS += -DSDIO_ISR_THREAD
endif
endif

ifneq ($(CONFIG_BCMDHD_PCIE),)
DHDCFLAGS += \
	-DPCIE_FULL_DONGLE -DBCMPCIE -DSHOW_LOGTRACE -DDPCIE_TX_DEFERRAL      \
	-DCUSTOM_DPC_PRIO_SETTING=-1

DHDOFILES += dhd_pcie.o dhd_pcie_linux.o pcie_core.o dhd_flowring.o \
	dhd_msgbuf.o
endif

$(MODULE_NAME)-y += $(DHDOFILES)

#ifeq ($(CONFIG_MACH_ODROID_4210),y)
DHDOFILES += dhd_gpio.o
DHDCFLAGS += -DCUSTOMER_HW -DDHD_OF_SUPPORT -DGET_CUSTOM_MAC_ENABLE
#DHDCFLAGS += -DBCMWAPI_WPI -DBCMWAPI_WAI
#endif

#DHDCFLAGS += -DBAND_AG

ifeq ($(CONFIG_DHD_USE_STATIC_BUF),y)
# add dhd_static_buf to kernel image build
#obj-y += dhd_static_buf.o
DHDCFLAGS += -DSTATIC_WL_PRIV_STRUCT -DENHANCED_STATIC_BUF
endif

ifneq ($(CONFIG_WIRELESS_EXT),)
bcmdhd-objs += wl_iw.o
DHDCFLAGS += -DSOFTAP -DWL_WIRELESS_EXT -DUSE_IW
endif
ifneq ($(CONFIG_CFG80211),)
bcmdhd-objs += wl_cfg80211.o wl_cfgp2p.o wl_linux_mon.o wl_cfg_btcoex.o
bcmdhd-objs += dhd_cfg80211.o dhd_cfg_vendor.o
DHDCFLAGS += -DWL_CFG80211 -DWLP2P -DWL_CFG80211_STA_EVENT -DWL_ENABLE_P2P_IF
DHDCFLAGS += -DWL_IFACE_COMB_NUM_CHANNELS
DHDCFLAGS += -DCUSTOM_ROAM_TRIGGER_SETTING=-65
DHDCFLAGS += -DCUSTOM_ROAM_DELTA_SETTING=15
DHDCFLAGS += -DCUSTOM_KEEP_ALIVE_SETTING=28000
DHDCFLAGS += -DCUSTOM_PNO_EVENT_LOCK_xTIME=7
DHDCFLAGS += -DWL_SUPPORT_AUTO_CHANNEL
DHDCFLAGS += -DWL_SUPPORT_BACKPORTED_KPATCHES
DHDCFLAGS += -DESCAN_RESULT_PATCH
DHDCFLAGS += -DVSDB -DWL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
DHDCFLAGS += -DWLTDLS -DMIRACAST_AMPDU_SIZE=8 -DPROP_TXSTATUS_VSDB
endif
EXTRA_CFLAGS = $(DHDCFLAGS)
ifeq ($(CONFIG_BCMDHD),m)
DHDCFLAGS += -DMULTIPLE_SUPPLICANT
EXTRA_LDFLAGS += --strip-debug
else
DHDCFLAGS += -DBUILD_IN_KERNEL
endif

$(MODULE_NAME)-y += rkversion.o

obj-$(CONFIG_RKWIFI) += $(MODULE_NAME).o

KERNEL_DIR = /home/duke/jb_4.2/kernel

all:
	make -j4 -C $(KERNEL_DIR) M=`pwd` modules
	mv wlan.ko rkwifi.oob.ko
clean:
	rm -fr *.mod.c *.mod *.o .*.cmd *.ko *~
	rm .tmp_versions -fr ; rm Module.symvers -fr
	rm -fr Module.markers ; rm -fr modules.order
