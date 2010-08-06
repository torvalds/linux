obj-$(CONFIG_IWM) := iwmc3200wifi.o
iwmc3200wifi-objs += main.o netdev.o rx.o tx.o sdio.o hal.o fw.o
iwmc3200wifi-objs += commands.o cfg80211.o eeprom.o

iwmc3200wifi-$(CONFIG_IWM_DEBUG) += debugfs.o
iwmc3200wifi-$(CONFIG_IWM_TRACING) += trace.o

CFLAGS_trace.o := -I$(src)

ccflags-y += -D__CHECK_ENDIAN__
