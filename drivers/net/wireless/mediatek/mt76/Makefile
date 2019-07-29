obj-$(CONFIG_MT76_CORE) += mt76.o
obj-$(CONFIG_MT76_USB) += mt76-usb.o
obj-$(CONFIG_MT76x02_LIB) += mt76x02-lib.o
obj-$(CONFIG_MT76x02_USB) += mt76x02-usb.o

mt76-y := \
	mmio.o util.o trace.o dma.o mac80211.o debugfs.o eeprom.o tx.o agg-rx.o

mt76-usb-y := usb.o usb_trace.o usb_mcu.o

CFLAGS_trace.o := -I$(src)
CFLAGS_usb_trace.o := -I$(src)
CFLAGS_mt76x02_trace.o := -I$(src)

mt76x02-lib-y := mt76x02_util.o mt76x02_mac.o mt76x02_mcu.o \
		 mt76x02_eeprom.o mt76x02_phy.o mt76x02_mmio.o \
		 mt76x02_txrx.o mt76x02_trace.o mt76x02_debugfs.o \
		 mt76x02_dfs.o

mt76x02-usb-y := mt76x02_usb_mcu.o mt76x02_usb_core.o

obj-$(CONFIG_MT76x0_COMMON) += mt76x0/
obj-$(CONFIG_MT76x2_COMMON) += mt76x2/
