#
# Makefile for the linux kernel, U8500 machine.
#

obj-y				:= pm.o
obj-$(CONFIG_UX500_SOC_DB8500)	+= cpu-db8500.o
obj-$(CONFIG_MACH_MOP500)	+= board-mop500-audio.o
obj-$(CONFIG_SMP)		+= platsmp.o
obj-$(CONFIG_HOTPLUG_CPU)	+= hotplug.o
obj-$(CONFIG_PM_GENERIC_DOMAINS) += pm_domains.o

CFLAGS_hotplug.o		+= -march=armv7-a
