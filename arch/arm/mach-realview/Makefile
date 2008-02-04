#
# Makefile for the linux kernel.
#

obj-y					:= core.o clock.o
obj-$(CONFIG_MACH_REALVIEW_EB)		+= realview_eb.o
obj-$(CONFIG_SMP)			+= platsmp.o headsmp.o localtimer.o
obj-$(CONFIG_HOTPLUG_CPU)		+= hotplug.o
