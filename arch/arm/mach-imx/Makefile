#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y			+= irq.o time.o dma.o generic.o

obj-$(CONFIG_CPU_FREQ_IMX)	+= cpufreq.o

# Specific board support
obj-$(CONFIG_ARCH_MX1ADS) += mx1ads.o

# Support for blinky lights
led-y := leds.o

obj-$(CONFIG_LEDS)	+=  $(led-y)
led-$(CONFIG_ARCH_MX1ADS) += leds-mx1ads.o
