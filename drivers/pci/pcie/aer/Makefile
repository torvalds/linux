# SPDX-License-Identifier: GPL-2.0
#
# Makefile for PCI-Express Root Port Advanced Error Reporting Driver
#

obj-$(CONFIG_PCIEAER) += aerdriver.o

obj-$(CONFIG_PCIE_ECRC)	+= ecrc.o

aerdriver-objs := aerdrv_errprint.o aerdrv_core.o aerdrv.o
aerdriver-$(CONFIG_ACPI) += aerdrv_acpi.o

obj-$(CONFIG_PCIEAER_INJECT) += aer_inject.o
