
RKWLCFGDIR = ../wifi_power
BROADCOM_SRC_DIR = drivers/net/wireless/bcm4319

EXTRA_CFLAGS += -I$(src) -I$(src)/include -I$(src)/$(RKWLCFGDIR)/
EXTRA_CFLAGS += -DLINUX -DSRCBASE=\"$(BROADCOM_SRC_DIR)\" -DBCMDRIVER -DBCMDONGLEHOST -DDHDTHREAD\
                -DBCMWPA2 -DBCMWAPI_WPI
EXTRA_CFLAGS += -DUNRELEASEDCHIP -DOEM_ANDROID -DEMBEDDED_PLATFORM -DARP_OFFLOAD_SUPPORT -DPKT_FILTER_SUPPORT 
EXTRA_CFLAGS += -DBCMSDIO -DDHD_GPL -DBCMLXSDMMC -DBCMPLATFORM_BUS
EXTRA_CFLAGS += -DSDIO_ISR_THREAD
EXTRA_CFLAGS += -DBDC -DTOE -DDHD_BCMEVENTS -DDHD_SCHED -DCSCAN
#EXTRA_CFLAGS += -DSHOW_EVENTS -DDHD_DEBUG -DSDTEST 
#EXTRA_CFLAGS += -DBCMSDH_MODULE
#EXTRA_CFLAGS += -DCONFIG_WIFI_CONTROL_FUNC


bcmsdio-objs := bcmsdh.o bcmsdh_linux.o bcmsdh_sdmmc.o \
                bcmsdh_sdmmc_linux.o

dhd-objs := dhd_common.o  dhd_linux.o dhd_linux_sched.o dhd_sdio.o \
            dhd_custom_gpio.o dhd_cdc.o

shared-objs := aiutils.o bcmutils.o bcmwifi.o hndpmu.o \
               linux_osl.o  sbutils.o siutils.o

wl-objs := wl_iw.o

bcm4319-objs := $(bcmsdio-objs) $(dhd-objs) $(shared-objs) $(wl-objs) $(RKWLCFGDIR)/wifi_power_ops.o

obj-$(CONFIG_BCM4319) += bcm4319.o $(RKWLCFGDIR)/wifi_power.o

default:
	$(MAKE) -C $(KERNELDIR) SUBDIRS=$(PWD) modules
clean:
	rm -rvf *.o *.ko *.mod.c *.o.cmd *.tmp_versions *.mod.o *.cmd modules.order Module.symvers .tmp_versions rkpub
	find . -name '*.o' -exec rm -f {} \;
	find . -name '.*.cmd' -exec rm -f {} \;
.PHONY: clean



                 
