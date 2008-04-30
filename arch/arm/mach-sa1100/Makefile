#
# Makefile for the linux kernel.
#

# Common support
obj-y := clock.o generic.o gpio.o irq.o dma.o time.o #nmi-oopser.o
obj-m :=
obj-n :=
obj-  :=
led-y := leds.o

obj-$(CONFIG_CPU_FREQ_SA1100)		+= cpu-sa1100.o
obj-$(CONFIG_CPU_FREQ_SA1110)		+= cpu-sa1110.o

# Specific board support
obj-$(CONFIG_SA1100_ASSABET)		+= assabet.o
led-$(CONFIG_SA1100_ASSABET)		+= leds-assabet.o
obj-$(CONFIG_ASSABET_NEPONSET)		+= neponset.o

obj-$(CONFIG_SA1100_BADGE4)		+= badge4.o
led-$(CONFIG_SA1100_BADGE4)		+= leds-badge4.o

obj-$(CONFIG_SA1100_CERF)		+= cerf.o
led-$(CONFIG_SA1100_CERF)		+= leds-cerf.o

obj-$(CONFIG_SA1100_COLLIE)		+= collie.o

obj-$(CONFIG_SA1100_H3600)		+= h3600.o

obj-$(CONFIG_SA1100_HACKKIT)		+= hackkit.o
led-$(CONFIG_SA1100_HACKKIT)		+= leds-hackkit.o

obj-$(CONFIG_SA1100_JORNADA720)		+= jornada720.o
obj-$(CONFIG_SA1100_JORNADA720_SSP)	+= jornada720_ssp.o

obj-$(CONFIG_SA1100_LART)		+= lart.o
led-$(CONFIG_SA1100_LART)		+= leds-lart.o

obj-$(CONFIG_SA1100_PLEB)		+= pleb.o

obj-$(CONFIG_SA1100_SHANNON)		+= shannon.o

obj-$(CONFIG_SA1100_SIMPAD)		+= simpad.o
led-$(CONFIG_SA1100_SIMPAD)		+= leds-simpad.o

# LEDs support
obj-$(CONFIG_LEDS) += $(led-y)

# SA1110 USB client support
#obj-$(CONFIG_SA1100_USB)		+= usb/

# Miscelaneous functions
obj-$(CONFIG_PM)			+= pm.o sleep.o
obj-$(CONFIG_SA1100_SSP)		+= ssp.o

