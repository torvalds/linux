# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the i2c core.
#

obj-$(CONFIG_I2C_BOARDINFO)	+= i2c-boardinfo.o
obj-$(CONFIG_I2C)		+= i2c-core.o
i2c-core-objs 			:= i2c-core-base.o i2c-core-smbus.o
i2c-core-$(CONFIG_ACPI)		+= i2c-core-acpi.o
i2c-core-$(CONFIG_I2C_SLAVE) 	+= i2c-core-slave.o
i2c-core-$(CONFIG_OF) 		+= i2c-core-of.o

obj-$(CONFIG_I2C_SMBUS)		+= i2c-smbus.o
obj-$(CONFIG_I2C_CHARDEV)	+= i2c-dev.o
obj-$(CONFIG_I2C_MUX)		+= i2c-mux.o
obj-y				+= algos/ busses/ muxes/
obj-$(CONFIG_I2C_STUB)		+= i2c-stub.o
obj-$(CONFIG_I2C_SLAVE_EEPROM)	+= i2c-slave-eeprom.o

ccflags-$(CONFIG_I2C_DEBUG_CORE) := -DDEBUG
