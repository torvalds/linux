rsi_91x-y			+= rsi_91x_main.o
rsi_91x-y			+= rsi_91x_core.o
rsi_91x-y			+= rsi_91x_mac80211.o
rsi_91x-y			+= rsi_91x_mgmt.o
rsi_91x-y			+= rsi_91x_hal.o
rsi_91x-$(CONFIG_RSI_DEBUGFS)	+= rsi_91x_debugfs.o

rsi_usb-y			+= rsi_91x_usb.o rsi_91x_usb_ops.o
rsi_sdio-y			+= rsi_91x_sdio.o rsi_91x_sdio_ops.o
obj-$(CONFIG_RSI_91X) 		+= rsi_91x.o
obj-$(CONFIG_RSI_SDIO)		+= rsi_sdio.o
obj-$(CONFIG_RSI_USB)		+= rsi_usb.o
