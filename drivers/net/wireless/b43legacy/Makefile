obj-$(CONFIG_B43LEGACY) += b43legacy.o
b43legacy-obj-$(CONFIG_B43LEGACY_DEBUG) += debugfs.o

b43legacy-obj-$(CONFIG_B43LEGACY_DMA) += dma.o
b43legacy-obj-$(CONFIG_B43LEGACY_PIO) += pio.o

b43legacy-objs := main.o \
		ilt.o \
		leds.o \
		phy.o \
		radio.o \
		sysfs.o \
		xmit.o \
		$(b43legacy-obj-y)
