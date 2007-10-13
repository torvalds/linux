rt2x00lib-objs := rt2x00dev.o rt2x00mac.o rt2x00config.o

ifeq ($(CONFIG_RT2X00_LIB_DEBUGFS),y)
	rt2x00lib-objs += rt2x00debug.o
endif

ifeq ($(CONFIG_RT2X00_LIB_RFKILL),y)
	rt2x00lib-objs += rt2x00rfkill.o
endif

ifeq ($(CONFIG_RT2X00_LIB_FIRMWARE),y)
	rt2x00lib-objs += rt2x00firmware.o
endif

obj-$(CONFIG_RT2X00_LIB)	+= rt2x00lib.o
obj-$(CONFIG_RT2X00_LIB_PCI)	+= rt2x00pci.o
obj-$(CONFIG_RT2X00_LIB_USB)	+= rt2x00usb.o
obj-$(CONFIG_RT2400PCI)		+= rt2400pci.o
obj-$(CONFIG_RT2500PCI)		+= rt2500pci.o
obj-$(CONFIG_RT61PCI)		+= rt61pci.o
obj-$(CONFIG_RT2500USB)		+= rt2500usb.o
obj-$(CONFIG_RT73USB)		+= rt73usb.o
