# SPDX-License-Identifier: GPL-2.0
# sensor drivers
obj-$(CONFIG_ANGLE_DEVICE)                    += angle/
obj-$(CONFIG_GSENSOR_DEVICE)			+= accel/
obj-$(CONFIG_COMPASS_DEVICE)			+= compass/
obj-$(CONFIG_GYROSCOPE_DEVICE)			+= gyro/
obj-$(CONFIG_LIGHT_DEVICE)				+= lsensor/
obj-$(CONFIG_PROXIMITY_DEVICE)			+= psensor/
obj-$(CONFIG_TEMPERATURE_DEVICE)		+= temperature/
obj-$(CONFIG_PRESSURE_DEVICE)			+= pressure/
obj-$(CONFIG_HALL_DEVICE)			+= hall/


obj-$(CONFIG_SENSOR_DEVICE)			+= sensor_dev.o
sensor_dev-y = sensor-i2c.o sensor-dev.o
