# SPDX-License-Identifier: GPL-2.0-or-later
#
# Makefile for linux/drivers/platform/wmi
# ACPI WMI core
#

wmi-y			:= core.o marshalling.o string.o
obj-$(CONFIG_ACPI_WMI)	+= wmi.o

# Unit tests
obj-y			+= tests/
