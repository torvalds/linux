#
# Makefile for the linux kernel.
#

obj-y		:= clock.o irq.o time.o gpio.o common.o devices.o
obj-m		:=
obj-n		:=
obj-		:=

# Board-specific support
obj-$(CONFIG_ARCH_AT91RM9200DK)	+= board-dk.o
obj-$(CONFIG_MACH_AT91RM9200EK)	+= board-ek.o
obj-$(CONFIG_MACH_CSB337)	+= board-csb337.o
obj-$(CONFIG_MACH_CSB637)	+= board-csb637.o
#obj-$(CONFIG_MACH_CARMEVA)	+= board-carmeva.o
#obj-$(CONFIG_MACH_KB9200)	+= board-kb9202.o

# LEDs support
#led-$(CONFIG_ARCH_AT91RM9200DK)	+= leds.o
#led-$(CONFIG_MACH_AT91RM9200EK)	+= leds.o
#led-$(CONFIG_MACH_CSB337)	+= leds.o
#led-$(CONFIG_MACH_CSB637)	+= leds.o
#led-$(CONFIG_MACH_KB9200)	+= leds.o
obj-$(CONFIG_LEDS) += $(led-y)

# VGA support
#obj-$(CONFIG_FB_S1D13XXX)	+= ics1523.o
