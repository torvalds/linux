# bcmdhd
DHDCFLAGS = -Wall -Wstrict-prototypes -Werror -Dlinux -DBCMDRIVER             \
	-DBCMDONGLEHOST -DUNRELEASEDCHIP -DBCMDMA32 -DWLBTAMP -DBCMFILEIMAGE  \
	-DDHDTHREAD -DDHD_GPL -DDHD_SCHED -DDHD_DEBUG -DSDTEST -DBDC -DTOE    \
	-DDHD_BCMEVENTS -DSHOW_EVENTS -DDONGLEOVERLAYS -DOEM_ANDROID -DBCMDBG \
	-DCUSTOMER_HW3 -DCUSTOM_OOB_GPIO_NUM=135 -DOOB_INTR_ONLY              \
	-DMMC_SDIO_ABORT -DCONFIG_FIRST_SCAN -DBCMSDIO -DBCMLXSDMMC           \
	-DBCMPLATFORM_BUS -DHW_OOB -DNEW_COMPAT_WIRELESS -DWL_CFG80211        \
	-DWLP2P -DWIFI_ACT_FRAME -DARP_OFFLOAD_SUPPORT -DKEEP_ALIVE -DCSCAN   \
	-Idrivers/net/wireless/bcmdhd -Idrivers/net/wireless/bcmdhd/include

DHDOFILES = aiutils.o bcmsdh_sdmmc_linux.o dhd_linux.o siutils.o bcmutils.o   \
	dhd_linux_sched.o wl_cfg80211.o bcmwifi.o dhd_sdio.o wl_cfgp2p.o      \
	bcmevent.o dhd_bta.o hndpmu.o bcmsdh.o dhd_cdc.o bcmsdh_linux.o       \
	dhd_common.o linux_osl.o bcmsdh_sdmmc.o dhd_custom_gpio.o sbutils.o   \
	wldev_common.o

obj-$(CONFIG_BCMDHD) += bcmdhd.o
bcmdhd-objs += $(DHDOFILES)
ifneq ($(CONFIG_WIRELESS_EXT),)
bcmdhd-objs += wl_iw.o
endif
EXTRA_CFLAGS = $(DHDCFLAGS)
EXTRA_LDFLAGS += --strip-debug
