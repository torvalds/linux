# basic options (defines in EXTER_CFLAGS, includes in IFLAGS, option in AFLAGS)
EXTRA_CFLAGS 	= 	-Dlinux -DLINUX
EXTRA_CFLAGS 	+=	-DBCMDRIVER -DBCMDONGLEHOST -DBCMSDIO -DUNRELEASEDCHIP 		\
					-DDHD_SCHED -DBDC -DTOE -DDHD_BCMEVENTS -DSHOW_EVENTS		\
					-DWLBTAMP -DDHDTHREAD -DMMC_SDIO_ABORT  	\
					-DDHD_GPL -DWIFI_ACT_FRAME -DBCMLXSDMMC -DBCMPLATFORM_BUS	\
					-DPKT_FILTER_SUPPORT -DBCMDBG -DDHD_DEBUG -DSRCBASE=\"$(src)/src\"
# ADD FLAGS
AFLAGS += -DCUSTOMER_HW -DOEM_ANDROID -DDHD_SDALIGN=64 -DMAX_HDR_READ=64 -DDHD_FIRSTREAD=64
AFLAGS += -DOOB_INTR_ONLY -DHW_OOB -DCSCAN -DHARD_KERNEL -DOOB_TIMER_INTERVAL_NS=10000000
AFLAGS += -DSOFTAP -DWLP2P -DPNO_SUPPORT -DDONGLEOVERLAYS -DCONFIG_WIRELESS_EXT -DCUSTOM_OOB_GPIO_NUM=0
AFLAGS += -DGET_CUSTOM_MAC_ENABLE -DSET_RANDOM_MAC_SOFTAP

# LEAVE FLAGS
# AFLAGS += -DBCMSDH_MODULE -DOOB_INTR_ONLY -DHW_OOB -DCONFIG_FIRST_SCAN
# AFLAGS += -DGET_CUSTOM_MAC_ENABLE -DCSCAN -DSDIO_ISR_THREAD
# AFLAGS += -DWL_CFG80211 -DNEW_COMPAT_WIRELESS 

IFLAGS += -I$(src)/src/include/
IFLAGS += -I$(src)/src/dhd/sys/
IFLAGS += -I$(src)/src/dongle/
IFLAGS += -I$(src)/src/bcmsdio/sys/
IFLAGS += -I$(src)/src/wl/sys/
IFLAGS += -I$(src)/src/shared/

EXTRA_CFLAGS 	+= 	-Wall -Wstrict-prototypes $(IFLAGS)	$(AFLAGS)	
EXTRA_LDFLAGS	+= 	--strip-debug

CFILES :=	src/dhd/sys/dhd_bta.o 					\
         	src/dhd/sys/dhd_cdc.o 					\
         	src/dhd/sys/dhd_common.o 				\
         	src/dhd/sys/dhd_custom_gpio.o 			\
			src/dhd/sys/dhd_linux.o 				\
         	src/dhd/sys/dhd_linux_sched.o 			\
         	src/dhd/sys/dhd_sdio.o 					\
        	src/shared/aiutils.o 					\
         	src/shared/bcmevent.o 					\
         	src/shared/bcmutils.o 					\
         	src/shared/bcmwifi.o 					\
         	src/shared/hndpmu.o 					\
         	src/shared/linux_osl.o 					\
         	src/shared/sbutils.o 					\
         	src/shared/siutils.o 					\
         	src/bcmsdio/sys/bcmsdh.o 				\
         	src/bcmsdio/sys/bcmsdh_linux.o 			\
         	src/bcmsdio/sys/bcmsdh_sdmmc.o 			\
         	src/bcmsdio/sys/bcmsdh_sdmmc_linux.o 	\
         	src/wl/sys/wl_iw.o
         	
# LEAVE FILES
#         	src/shared/miniopt.o 					\
#         	src/bcmsdio/sys/bcmsdstd.o 				\
#         	src/bcmsdio/sys/bcmsdstd_linux.o 		\
#         	src/bcmsdio/sys/bcmspi.o 				\
#         	src/bcmsdio/sys/bcmspi_linux.o 			\
#         	src/wl/sys/uamp_linux.o 				\
#         	src/wl/sys/wl_cfg80211.o 				\
#         	src/wl/sys/wl_cfgp2p.o 					\

obj-$(CONFIG_BCM4330) 	+= bcmdhd.o
bcmdhd-objs 			+= $(CFILES)
bcmdhd-objs 			+= hardkernel.o
