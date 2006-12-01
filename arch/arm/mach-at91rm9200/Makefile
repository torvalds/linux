#
# Makefile for the linux kernel.
#

obj-y		:= clock.o irq.o gpio.o
obj-m		:=
obj-n		:=
obj-		:=

obj-$(CONFIG_PM)		+= pm.o

# CPU-specific support
obj-$(CONFIG_ARCH_AT91RM9200)	+= at91rm9200.o at91rm9200_time.o at91rm9200_devices.o
obj-$(CONFIG_ARCH_AT91SAM9260)	+= at91sam9260.o at91sam926x_time.o at91sam9260_devices.o
obj-$(CONFIG_ARCH_AT91SAM9261)	+= at91sam9261.o at91sam926x_time.o at91sam9261_devices.o

# AT91RM9200 board-specific support
obj-$(CONFIG_MACH_ONEARM)	+= board-1arm.o
obj-$(CONFIG_ARCH_AT91RM9200DK)	+= board-dk.o
obj-$(CONFIG_MACH_AT91RM9200EK)	+= board-ek.o
obj-$(CONFIG_MACH_CSB337)	+= board-csb337.o
obj-$(CONFIG_MACH_CSB637)	+= board-csb637.o
obj-$(CONFIG_MACH_CARMEVA)	+= board-carmeva.o
obj-$(CONFIG_MACH_KB9200)	+= board-kb9202.o
obj-$(CONFIG_MACH_ATEB9200)	+= board-eb9200.o
obj-$(CONFIG_MACH_KAFA)		+= board-kafa.o

# AT91SAM9260 board-specific support

# AT91SAM9261 board-specific support

# LEDs support
led-$(CONFIG_ARCH_AT91RM9200DK)	+= leds.o
led-$(CONFIG_MACH_AT91RM9200EK)	+= leds.o
led-$(CONFIG_MACH_CSB337)	+= leds.o
led-$(CONFIG_MACH_CSB637)	+= leds.o
led-$(CONFIG_MACH_KB9200)	+= leds.o
led-$(CONFIG_MACH_KAFA)		+= leds.o
obj-$(CONFIG_LEDS) += $(led-y)

# VGA support
#obj-$(CONFIG_FB_S1D13XXX)	+= ics1523.o


ifeq ($(CONFIG_PM_DEBUG),y)
CFLAGS_pm.o += -DDEBUG
endif
