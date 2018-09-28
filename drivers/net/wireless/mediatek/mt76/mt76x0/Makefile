obj-$(CONFIG_MT76x0U) += mt76x0u.o
obj-$(CONFIG_MT76x0E) += mt76x0e.o
obj-$(CONFIG_MT76x0_COMMON) += mt76x0-common.o

mt76x0-common-y := \
	init.o main.o trace.o eeprom.o phy.o \
	mac.o debugfs.o tx.o
mt76x0u-y := usb.o usb_mcu.o
mt76x0e-y := pci.o pci_mcu.o

# ccflags-y := -DDEBUG
CFLAGS_trace.o := -I$(src)
