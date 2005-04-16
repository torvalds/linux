#
# Makefile for the i2c core.
#

obj-$(CONFIG_I2C)		+= i2c-core.o
obj-$(CONFIG_I2C_CHARDEV)	+= i2c-dev.o
obj-$(CONFIG_I2C_SENSOR)	+= i2c-sensor.o
obj-y				+= busses/ chips/ algos/

i2c-sensor-objs := i2c-sensor-detect.o i2c-sensor-vid.o


ifeq ($(CONFIG_I2C_DEBUG_CORE),y)
EXTRA_CFLAGS += -DDEBUG
endif
