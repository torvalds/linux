# SPDX-License-Identifier: GPL-2.0
#
# Makefile for loongson based machines.
#

obj-y += setup.o init.o cmdline.o env.o time.o reset.o

obj-$(CONFIG_PCI) += pci.o

obj-$(CONFIG_LOONGSON_MC146818) += rtc.o

#
# Suspend Support
#

obj-$(CONFIG_SUSPEND) += pm.o
