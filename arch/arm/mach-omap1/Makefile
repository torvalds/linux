#
# Makefile for the linux kernel.
#

# Common support
obj-y := io.o id.o irq.o time.o serial.o devices.o
led-y := leds.o

# Specific board support
obj-$(CONFIG_MACH_OMAP_H2)		+= board-h2.o
obj-$(CONFIG_MACH_OMAP_INNOVATOR)	+= board-innovator.o
obj-$(CONFIG_MACH_OMAP_GENERIC)		+= board-generic.o
obj-$(CONFIG_MACH_OMAP_PERSEUS2)	+= board-perseus2.o
obj-$(CONFIG_MACH_OMAP_OSK)		+= board-osk.o
obj-$(CONFIG_MACH_OMAP_H3)		+= board-h3.o
obj-$(CONFIG_MACH_VOICEBLUE)		+= board-voiceblue.o
obj-$(CONFIG_MACH_NETSTAR)		+= board-netstar.o

ifeq ($(CONFIG_ARCH_OMAP1510),y)
# Innovator-1510 FPGA
obj-$(CONFIG_MACH_OMAP_INNOVATOR)	+= fpga.o
endif

# LEDs support
led-$(CONFIG_MACH_OMAP_H2)		+= leds-h2p2-debug.o
led-$(CONFIG_MACH_OMAP_H3)		+= leds-h2p2-debug.o
led-$(CONFIG_MACH_OMAP_INNOVATOR)	+= leds-innovator.o
led-$(CONFIG_MACH_OMAP_PERSEUS2)	+= leds-h2p2-debug.o
led-$(CONFIG_MACH_OMAP_OSK)		+= leds-osk.o
obj-$(CONFIG_LEDS)			+= $(led-y)

