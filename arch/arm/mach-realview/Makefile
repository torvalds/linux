#
# Makefile for the linux kernel.
#

obj-y					:= core.o clock.o
obj-$(CONFIG_MACH_REALVIEW_EB)		+= realview_eb.o
obj-$(CONFIG_MACH_REALVIEW_PB11MP)	+= realview_pb11mp.o
obj-$(CONFIG_MACH_REALVIEW_PB1176)	+= realview_pb1176.o
obj-$(CONFIG_MACH_REALVIEW_PBA8)	+= realview_pba8.o
obj-$(CONFIG_SMP)			+= platsmp.o headsmp.o localtimer.o
obj-$(CONFIG_HOTPLUG_CPU)		+= hotplug.o
