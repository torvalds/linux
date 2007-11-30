#
# Makefile for the linux kernel.
#

# Common support
obj-y := io.o id.o clock.o irq.o mux.o serial.o devices.o

obj-$(CONFIG_OMAP_MPU_TIMER)		+= time.o

# Power Management
obj-$(CONFIG_PM) += pm.o sleep.o

led-y := leds.o

# Specific board support
obj-$(CONFIG_MACH_OMAP_H2)		+= board-h2.o board-h2-mmc.o
obj-$(CONFIG_MACH_OMAP_INNOVATOR)	+= board-innovator.o
obj-$(CONFIG_MACH_OMAP_GENERIC)		+= board-generic.o
obj-$(CONFIG_MACH_OMAP_PERSEUS2)	+= board-perseus2.o
obj-$(CONFIG_MACH_OMAP_FSAMPLE)		+= board-fsample.o
obj-$(CONFIG_MACH_OMAP_OSK)		+= board-osk.o
obj-$(CONFIG_MACH_OMAP_H3)		+= board-h3.o board-h3-mmc.o
obj-$(CONFIG_MACH_VOICEBLUE)		+= board-voiceblue.o
obj-$(CONFIG_MACH_OMAP_PALMTE)		+= board-palmte.o
obj-$(CONFIG_MACH_OMAP_PALMZ71)		+= board-palmz71.o
obj-$(CONFIG_MACH_OMAP_PALMTT)		+= board-palmtt.o
obj-$(CONFIG_MACH_NOKIA770)		+= board-nokia770.o
obj-$(CONFIG_MACH_AMS_DELTA)		+= board-ams-delta.o
obj-$(CONFIG_MACH_SX1)			+= board-sx1.o board-sx1-mmc.o

ifeq ($(CONFIG_ARCH_OMAP15XX),y)
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
