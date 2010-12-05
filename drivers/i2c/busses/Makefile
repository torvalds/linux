#
# Makefile for the i2c bus drivers.
#
obj-$(CONFIG_I2C_RK2818)	+= i2c-rk2818.o
obj-$(CONFIG_I2C_RK29)		+= i2c-rk29.o
obj-$(CONFIG_I2C_DEV_RK29)		+= i2c-dev-rk29.o


ifeq ($(CONFIG_I2C_DEBUG_BUS),y)
EXTRA_CFLAGS += -DDEBUG
endif
