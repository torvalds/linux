# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for Loongson-3 family machines
#
obj-$(CONFIG_MACH_LOONGSON64) += irq.o cop2-ex.o platform.o acpi_init.o dma.o \
				setup.o init.o env.o time.o reset.o \

obj-$(CONFIG_SMP)	+= smp.o
obj-$(CONFIG_NUMA)	+= numa.o
obj-$(CONFIG_RS780_HPET) += hpet.o
obj-$(CONFIG_PCI) += pci.o
obj-$(CONFIG_LOONGSON_MC146818) += rtc.o
obj-$(CONFIG_SUSPEND) += pm.o
