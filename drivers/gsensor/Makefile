#
# Makefile for I2C gsensor chip drivers.
#

obj-$(CONFIG_MEMSIC_ECOMPASS)	+= mecs.o
obj-$(CONFIG_SENSORS_MXC6202X)	+= mxc6202x.o
obj-$(CONFIG_SENSORS_MXC622X)	+= mxc622x.o
obj-$(CONFIG_SENSORS_MMC31XX)	+= mmc31xx.o

obj-$(CONFIG_SENSORS_BMA250)		+= bma250.o

ifeq ($(CONFIG_I2C_DEBUG_CHIP),y)
EXTRA_CFLAGS += -DDEBUG
endif
