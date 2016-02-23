#
# Makefile for Apple Motion Sensor driver
#

ams-y					:= ams-core.o ams-input.o
ams-$(CONFIG_SENSORS_AMS_PMU)		+= ams-pmu.o
ams-$(CONFIG_SENSORS_AMS_I2C)		+= ams-i2c.o
obj-$(CONFIG_SENSORS_AMS)		+= ams.o
