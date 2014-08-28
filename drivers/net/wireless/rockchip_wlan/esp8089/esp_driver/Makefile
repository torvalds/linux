# copyright (c) 2011 - 2012 espressif system

DRIVER_NAME := wlan
include drivers/net/wireless/rockchip_wlan/esp8089/esp_driver/esp_config.mk

EXTRA_CFLAGS += -DDEBUG -DSIP_DEBUG -DFAST_TX_STATUS -DANDROID -DKERNEL_IV_WAR -DRX_SENDUP_SYNC -DDEBUGFS -DHAS_FW -DTEST_MODE -DESP_ANDROID_LOGGER -DLOOKAHEAD -DSIF_DSR_WAR -DESP_PREALLOC

obj-$(CONFIG_ESP8089) := $(DRIVER_NAME).o
$(DRIVER_NAME)-y += esp_debug.o
$(DRIVER_NAME)-y += sdio_sif_esp.o
$(DRIVER_NAME)-y += spi_sif_esp.o
$(DRIVER_NAME)-y += esp_io.o
$(DRIVER_NAME)-y += esp_android.o
$(DRIVER_NAME)-y += esp_main.o
$(DRIVER_NAME)-y += esp_sip.o
$(DRIVER_NAME)-y += esp_ext.o
$(DRIVER_NAME)-y += esp_ctrl.o
$(DRIVER_NAME)-y += esp_mac80211.o
$(DRIVER_NAME)-y += esp_debug.o
$(DRIVER_NAME)-y += esp_utils.o
$(DRIVER_NAME)-y += esp_pm.o
$(DRIVER_NAME)-y += testmode.o

$(DRIVER_NAME):
	$(MAKE) ARCH=$(ARCH) -C $(KSRC) M=$(MODPATH) modules

.PHONE:clean
clean:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KSRC) M=$(MODPATH) clean 

#else
#KDIR ?= /lib/modules/`uname -r`/build
##KDIR ?= /home/espressif/src/sdio/cleanup

#default:
#	$(MAKE) -C $(KDIR) M=$$PWD

#endif
