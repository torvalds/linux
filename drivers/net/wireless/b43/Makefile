# b43 core
b43-y				+= main.o
b43-y				+= tables.o
b43-y				+= phy.o
b43-y				+= sysfs.o
b43-y				+= xmit.o
b43-y				+= lo.o
# b43 RFKILL button support
b43-$(CONFIG_B43_RFKILL)	+= rfkill.o
# b43 LED support
b43-$(CONFIG_B43_LEDS)		+= leds.o
# b43 PCMCIA support
b43-$(CONFIG_B43_PCMCIA)	+= pcmcia.o
# b43 debugging
b43-$(CONFIG_B43_DEBUG)		+= debugfs.o
# b43 DMA and PIO
b43-$(CONFIG_B43_DMA)		+= dma.o
b43-$(CONFIG_B43_PIO)		+= pio.o

obj-$(CONFIG_B43)		+= b43.o
