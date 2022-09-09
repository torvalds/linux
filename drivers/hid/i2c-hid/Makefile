# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the I2C input drivers
#

obj-$(CONFIG_I2C_HID_CORE)			+= i2c-hid.o

i2c-hid-objs					=  i2c-hid-core.o
i2c-hid-$(CONFIG_DMI)				+= i2c-hid-dmi-quirks.o

obj-$(CONFIG_I2C_HID_ACPI)			+= i2c-hid-acpi.o
obj-$(CONFIG_I2C_HID_OF)			+= i2c-hid-of.o
obj-$(CONFIG_I2C_HID_OF_ELAN)			+= i2c-hid-of-elan.o
obj-$(CONFIG_I2C_HID_OF_GOODIX)			+= i2c-hid-of-goodix.o
