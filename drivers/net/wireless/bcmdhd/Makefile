# bcmdhd
#
#
#
#
#

DHDCFLAGS = -Wall -Wstrict-prototypes -Dlinux -DBCMDRIVER                     \
	-DBCMDONGLEHOST -DUNRELEASEDCHIP -DBCMDMA32 -DBCMFILEIMAGE            \
	-DDHDTHREAD -DDHD_DEBUG -DSDTEST -DBDC                                \
	-DDHD_BCMEVENTS -DSHOW_EVENTS -DPROP_TXSTATUS -DBCMDBG                \
	-DCUSTOMER_HW2 -DOOB_INTR_ONLY -DHW_OOB                               \
	-DMMC_SDIO_ABORT -DBCMSDIO -DBCMLXSDMMC -DBCMPLATFORM_BUS -DWLP2P     \
	-DWIFI_ACT_FRAME -DARP_OFFLOAD_SUPPORT                                \
	-DKEEP_ALIVE -DGET_CUSTOM_MAC_ENABLE -DPKT_FILTER_SUPPORT             \
	-DEMBEDDED_PLATFORM -DENABLE_INSMOD_NO_FW_LOAD -DPNO_SUPPORT          \
	-DDHD_USE_IDLECOUNT -DSET_RANDOM_MAC_SOFTAP -DROAM_ENABLE -DVSDB      \
	-DWL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST                            \
	-DESCAN_RESULT_PATCH -DSDIO_CRC_ERROR_FIX                             \
	-DDHD_DONOT_FORWARD_BCMEVENT_AS_NETWORK_PKT -DRXFRAME_THREAD          \
	-DSUPPORT_PM2_ONLY -DWLTDLS                                           \
	-DMIRACAST_AMPDU_SIZE=8                                               \
	-Idrivers/net/wireless/bcmdhd -Idrivers/net/wireless/bcmdhd/include   \
	-Idrivers/net/wireless/bcmdhd/common/include

DHDOFILES = aiutils.o bcmsdh_sdmmc_linux.o dhd_linux.o siutils.o bcmutils.o   \
	dhd_linux_sched.o dhd_sdio.o bcmwifi_channels.o bcmevent.o hndpmu.o   \
	bcmsdh.o dhd_cdc.o bcmsdh_linux.o dhd_common.o dhd_wlfc.o dhd_ip.o    \
	linux_osl.o bcmsdh_sdmmc.o dhd_custom_gpio.o sbutils.o wldev_common.o \
	wl_android.o dhd_pno.o

obj-$(CONFIG_BCMDHD) += bcmdhd.o
bcmdhd-objs += $(DHDOFILES)
ifneq ($(CONFIG_WIRELESS_EXT),)
bcmdhd-objs += wl_iw.o
DHDCFLAGS += -DSOFTAP -DWL_WIRELESS_EXT -DUSE_IW
endif
ifneq ($(CONFIG_CFG80211),)
bcmdhd-objs += wl_cfg80211.o wl_cfgp2p.o wl_linux_mon.o dhd_cfg80211.o
DHDCFLAGS += -DWL_CFG80211 -DWL_CFG80211_STA_EVENT -DWL_ENABLE_P2P_IF
DHDCFLAGS += -DWL_IFACE_COMB_NUM_CHANNELS
DHDCFLAGS += -DCUSTOM_ROAM_TRIGGER_SETTING=-75
DHDCFLAGS += -DCUSTOM_ROAM_DELTA_SETTING=10
DHDCFLAGS += -DCUSTOM_KEEP_ALIVE_SETTING=28000
DHDCFLAGS += -DCUSTOM_PNO_EVENT_LOCK_xTIME=10
endif
ifneq ($(CONFIG_DHD_USE_SCHED_SCAN),)
DHDCFLAGS += -DWL_SCHED_SCAN
endif
EXTRA_CFLAGS = $(DHDCFLAGS)
ifeq ($(CONFIG_BCMDHD),m)
EXTRA_LDFLAGS += --strip-debug
endif

DHDCFLAGS += -DCUSTOM_TDLS_RSSI_THRESHOLD_HIGH=-60
DHDCFLAGS += -DCUSTOM_TDLS_RSSI_THRESHOLD_LOW=-70
DHDCFLAGS += -DCUSTOM_TDLS_IDLE_MODE_SETTING=40000

#########################
# Chip dependent feature
#########################
ifneq ($(CONFIG_BCM4339),)
DHDCFLAGS += -DCUSTOM_GLOM_SETTING=8 -DCUSTOM_RXCHAIN=1
DHDCFLAGS += -DUSE_DYNAMIC_F2_BLKSIZE -DDYNAMIC_F2_BLKSIZE_FOR_NONLEGACY=128
DHDCFLAGS += -DBCMSDIOH_TXGLOM -DCUSTOM_TXGLOM=1 -DBCMSDIOH_TXGLOM_HIGHSPEED
DHDCFLAGS += -DCUSTOM_SDIO_F2_BLKSIZE=512
DHDCFLAGS += -DDHDTCPACK_SUPPRESS
DHDCFLAGS += -DUSE_WL_TXBF
DHDCFLAGS += -DUSE_WL_FRAMEBURST
DHDCFLAGS += -DCUSTOM_AMPDU_BA_WSIZE=32
DHDCFLAGS += -DPROP_TXSTATUS_VSDB
DHDCFLAGS += -DCUSTOM_MAX_TXGLOM_SIZE=32
DHDCFLAGS += -DREPEAT_READFRAME
DHDCFLAGS += -DROAM_AP_ENV_DETECTION
DHDCFLAGS += -DWL11U
else
DHDCFLAGS += -DCUSTOM_SDIO_F2_BLKSIZE=128
endif
