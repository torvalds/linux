# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the linux kernel.
#

# versatile
obj-$(CONFIG_ARCH_VERSATILE)		+= versatile.o

# integrator
obj-$(CONFIG_ARCH_INTEGRATOR)		+= integrator.o
obj-$(CONFIG_ARCH_INTEGRATOR_AP)	+= integrator_ap.o
obj-$(CONFIG_ARCH_INTEGRATOR_CP)	+= integrator_cp.o

# realview
obj-$(CONFIG_ARCH_REALVIEW)		+= realview.o

# vexpress
obj-$(CONFIG_ARCH_VEXPRESS)		:= v2m.o
obj-$(CONFIG_ARCH_VEXPRESS_DCSCB)	+= dcscb.o	dcscb_setup.o
CFLAGS_dcscb.o				+= -march=armv7-a
CFLAGS_REMOVE_dcscb.o			= -pg
obj-$(CONFIG_ARCH_VEXPRESS_SPC)		+= spc.o
CFLAGS_REMOVE_spc.o			= -pg
obj-$(CONFIG_ARCH_VEXPRESS_TC2_PM)	+= tc2_pm.o
CFLAGS_tc2_pm.o				+= -march=armv7-a
CFLAGS_REMOVE_tc2_pm.o			= -pg

# mps2
obj-$(CONFIG_ARCH_MPS2)			+= v2m-mps2.o

ifdef CONFIG_SMP
obj-y					+= headsmp.o platsmp.o
obj-$(CONFIG_ARCH_REALVIEW)		+= platsmp-realview.o
obj-$(CONFIG_ARCH_VEXPRESS)		+= platsmp-vexpress.o
obj-$(CONFIG_HOTPLUG_CPU)		+= hotplug.o
endif
