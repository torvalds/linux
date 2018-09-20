obj-$(CONFIG_MT76_CORE) += mt76.o
obj-$(CONFIG_MT76_USB) += mt76-usb.o
obj-$(CONFIG_MT76x0_COMMON) += mt76x0/
obj-$(CONFIG_MT76x02_LIB) += mt76x02-lib.o
obj-$(CONFIG_MT76x02_USB) += mt76x02-usb.o
obj-$(CONFIG_MT76x2_COMMON) += mt76x2-common.o
obj-$(CONFIG_MT76x2E) += mt76x2e.o
obj-$(CONFIG_MT76x2U) += mt76x2u.o

mt76-y := \
	mmio.o util.o trace.o dma.o mac80211.o debugfs.o eeprom.o tx.o agg-rx.o

mt76-usb-y := usb.o usb_trace.o usb_mcu.o

CFLAGS_trace.o := -I$(src)
CFLAGS_usb_trace.o := -I$(src)

mt76x02-lib-y := mt76x02_util.o mt76x02_mac.o mt76x02_mcu.o \
		 mt76x02_eeprom.o

mt76x02-usb-y := mt76x02_usb_mcu.o mt76x02_usb_core.o

mt76x2-common-y := \
	mt76x2_eeprom.o mt76x2_tx_common.o mt76x2_mac_common.o \
	mt76x2_init_common.o mt76x2_common.o mt76x2_phy_common.o \
	mt76x2_debugfs.o mt76x2_mcu_common.o

mt76x2e-y := \
	mt76x2_pci.o mt76x2_dma.o \
	mt76x2_main.o mt76x2_init.o mt76x2_tx.o \
	mt76x2_core.o mt76x2_mac.o mt76x2_mcu.o mt76x2_phy.o \
	mt76x2_dfs.o mt76x2_trace.o

mt76x2u-y := \
	mt76x2_usb.o mt76x2u_init.o mt76x2u_main.o mt76x2u_mac.o \
	mt76x2u_mcu.o mt76x2u_phy.o mt76x2u_core.o

CFLAGS_mt76x2_trace.o := -I$(src)
