# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#
ccflags-$(CONFIG_ARCH_MULTIPLATFORM) := \
	-I$(srctree)/arch/arm/plat-versatile/include

obj-$(CONFIG_ARCH_VEXPRESS)		:= v2m.o
obj-$(CONFIG_ARCH_VEXPRESS_DCSCB)	+= dcscb.o	dcscb_setup.o
CFLAGS_dcscb.o				+= -march=armv7-a
CFLAGS_REMOVE_dcscb.o			= -pg
obj-$(CONFIG_ARCH_VEXPRESS_SPC)		+= spc.o
CFLAGS_REMOVE_spc.o			= -pg
obj-$(CONFIG_ARCH_VEXPRESS_TC2_PM)	+= tc2_pm.o
CFLAGS_tc2_pm.o				+= -march=armv7-a
CFLAGS_REMOVE_tc2_pm.o			= -pg
obj-$(CONFIG_SMP)			+= platsmp.o
obj-$(CONFIG_HOTPLUG_CPU)		+= hotplug.o

obj-$(CONFIG_ARCH_MPS2)			+= v2m-mps2.o
