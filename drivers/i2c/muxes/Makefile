#
# Makefile for multiplexer I2C chip drivers.

obj-$(CONFIG_I2C_MUX_GPIO)	+= gpio-i2cmux.o
obj-$(CONFIG_I2C_MUX_PCA9541)	+= pca9541.o
obj-$(CONFIG_I2C_MUX_PCA954x)	+= pca954x.o

ccflags-$(CONFIG_I2C_DEBUG_BUS) := -DDEBUG
