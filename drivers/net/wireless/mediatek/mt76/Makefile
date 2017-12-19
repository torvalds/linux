obj-$(CONFIG_MT76_CORE) += mt76.o
obj-$(CONFIG_MT76x2E) += mt76x2e.o

mt76-y := \
	mmio.o util.o trace.o dma.o mac80211.o debugfs.o eeprom.o tx.o

CFLAGS_trace.o := -I$(src)

mt76x2e-y := \
	mt76x2_pci.o mt76x2_dma.o \
	mt76x2_main.o mt76x2_init.o mt76x2_debugfs.o mt76x2_tx.o \
	mt76x2_core.o mt76x2_mac.o mt76x2_eeprom.o mt76x2_mcu.o mt76x2_phy.o \
	mt76x2_dfs.o mt76x2_trace.o

CFLAGS_mt76x2_trace.o := -I$(src)
