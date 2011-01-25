EXTRA_CFLAGS += -Idrivers/net/wireless/bcm4329 -Idrivers/net/wireless/bcm4329/include  \
                -DLINUX -DSRCBASE=\"$(BROADCOM_SRC_DIR)\" -DBCMDRIVER -DBCMDONGLEHOST -DDHDTHREAD \
                -DBCMWPA2 -DBCMWAPI_WPI -DUNRELEASEDCHIP -DOEM_ANDROID -DEMBEDDED_PLATFORM  \
                -DARP_OFFLOAD_SUPPORT -DPKT_FILTER_SUPPORT -DBCMSDIO -DDHD_GPL -DBCMLXSDMMC \
                -DBCMPLATFORM_BUS -DSDIO_ISR_THREAD -DBDC -DTOE -DDHD_BCMEVENTS -DSHOW_EVENTS  \
                -DDHD_SCHED -DDHD_DEBUG -DSDTEST -Wall -Wstrict-prototypes -Werror
 

bcmsdio-objs := bcmsdh.o bcmsdh_linux.o bcmsdh_sdmmc.o \
                bcmsdh_sdmmc_linux.o

dhd-objs := dhd_common.o  dhd_linux.o dhd_linux_sched.o dhd_sdio.o \
            dhd_custom_gpio.o dhd_cdc.o

shared-objs := aiutils.o bcmutils.o bcmwifi.o hndpmu.o \
               linux_osl.o  sbutils.o siutils.o

wl-objs := wl_iw.o

bcm4329-objs := $(bcmsdio-objs) $(dhd-objs) $(shared-objs) $(wl-objs) 

obj-$(CONFIG_BCM4329) += bcm4329.o









                 
