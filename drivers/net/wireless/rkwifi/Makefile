DHDCFLAGS = -Wall -Wstrict-prototypes -Dlinux -DBCMDRIVER                     \
	-DBCMDONGLEHOST -DUNRELEASEDCHIP -DBCMDMA32 -DWLBTAMP -DBCMFILEIMAGE  \
	-DDHDTHREAD -DDHD_GPL -DDHD_SCHED -DSDTEST -DBDC -DTOE    \
	-DDHD_BCMEVENTS -DSHOW_EVENTS -DDONGLEOVERLAYS -DBCMDBG               \
	-DCUSTOMER_HW2 -DKEEP_ALIVE -DCSCAN -DEMBEDDED_PLATFORM   \
	-DMMC_SDIO_ABORT -DBCMSDIO -DBCMLXSDMMC -DBCMPLATFORM_BUS -DWLP2P     \
	-DNEW_COMPAT_WIRELESS -DWIFI_ACT_FRAME -DSOFTAP        \
	-DENABLE_INSMOD_NO_FW_LOAD \
	-Idrivers/net/wireless/rkwifi -Idrivers/net/wireless/rkwifi/include

DHDOFILES = aiutils.o bcmsdh_sdmmc_linux.o dhd_linux.o siutils.o bcmutils.o   \
	dhd_linux_sched.o bcmwifi.o dhd_sdio.o bcmevent.o dhd_bta.o hndpmu.o  \
	bcmsdh.o dhd_cdc.o bcmsdh_linux.o dhd_common.o linux_osl.o  wl_iw.o   \
	bcmsdh_sdmmc.o dhd_custom_gpio.o sbutils.o wldev_common.o wl_android.o

obj-$(CONFIG_RKWIFI) += bcmdhd.o
bcmdhd-objs += $(DHDOFILES)
ifneq ($(CONFIG_RK_CFG80211),)
bcmdhd-objs += wl_cfg80211.o dhd_cfg80211.o wl_cfgp2p.o wl_linux_mon.o
DHDCFLAGS += -DWL_CFG80211
#WAPI support
#DHDCFLAGS += -DBCMWAPI_WPI -DBCMWAPI_WAI
endif
EXTRA_CFLAGS = $(DHDCFLAGS)
ifneq ($(CONFIG_RKWIFI),)
DHDCFLAGS += -I$(M) -I$(M)/include
EXTRA_LDFLAGS += --strip-debug
endif
