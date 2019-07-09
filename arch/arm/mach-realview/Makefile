# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the linux kernel.
#
ccflags-$(CONFIG_ARCH_MULTIPLATFORM) := -I$(srctree)/arch/arm/plat-versatile/include

obj-y					+= realview-dt.o
obj-$(CONFIG_SMP)			+= platsmp-dt.o
