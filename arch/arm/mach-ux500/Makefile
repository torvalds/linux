#
# Makefile for the linux kernel, U8500 machine.
#

obj-y				:= cpu.o devices.o devices-common.o \
				   id.o usb.o timer.o
obj-$(CONFIG_CPU_IDLE)          += cpuidle.o
obj-$(CONFIG_CACHE_L2X0)	+= cache-l2x0.o
obj-$(CONFIG_UX500_SOC_DB8500)	+= cpu-db8500.o devices-db8500.o
obj-$(CONFIG_MACH_MOP500)	+= board-mop500.o board-mop500-sdi.o \
				board-mop500-regulators.o \
				board-mop500-uib.o board-mop500-stuib.o \
				board-mop500-u8500uib.o \
				board-mop500-pins.o \
				board-mop500-audio.o
obj-$(CONFIG_SMP)		+= platsmp.o headsmp.o
obj-$(CONFIG_HOTPLUG_CPU)	+= hotplug.o
