# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for sensors seen through the ChromeOS EC sensor hub.
#

cros-ec-sensors-core-objs += cros_ec_sensors_core.o cros_ec_sensors_trace.o
obj-$(CONFIG_IIO_CROS_EC_SENSORS_CORE) += cros-ec-sensors-core.o
obj-$(CONFIG_IIO_CROS_EC_SENSORS) += cros_ec_sensors.o
obj-$(CONFIG_IIO_CROS_EC_SENSORS_LID_ANGLE) += cros_ec_lid_angle.o
obj-$(CONFIG_IIO_CROS_EC_ACTIVITY) += cros_ec_activity.o
