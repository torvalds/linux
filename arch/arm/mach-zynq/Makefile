#
# Makefile for the linux kernel.
#

# Common support
obj-y				:= common.o slcr.o
CFLAGS_REMOVE_hotplug.o		=-march=armv6k
CFLAGS_hotplug.o 		=-Wa,-march=armv7-a -mcpu=cortex-a9
obj-$(CONFIG_HOTPLUG_CPU)	+= hotplug.o
obj-$(CONFIG_SMP)		+= headsmp.o platsmp.o
