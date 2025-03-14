# SPDX-License-Identifier: GPL-2.0
#
# Makefile for industrial I/O Magnetometer sensor drivers
#

# When adding new entries keep the list in alphabetical order
obj-$(CONFIG_AF8133J)	+= af8133j.o
obj-$(CONFIG_AK8974)	+= ak8974.o
obj-$(CONFIG_AK8975)	+= ak8975.o
obj-$(CONFIG_ALS31300)	+= als31300.o
obj-$(CONFIG_BMC150_MAGN) += bmc150_magn.o
obj-$(CONFIG_BMC150_MAGN_I2C) += bmc150_magn_i2c.o
obj-$(CONFIG_BMC150_MAGN_SPI) += bmc150_magn_spi.o

obj-$(CONFIG_MAG3110)	+= mag3110.o
obj-$(CONFIG_HID_SENSOR_MAGNETOMETER_3D) += hid-sensor-magn-3d.o
obj-$(CONFIG_MMC35240)	+= mmc35240.o

obj-$(CONFIG_IIO_ST_MAGN_3AXIS) += st_magn.o
st_magn-y := st_magn_core.o
st_magn-$(CONFIG_IIO_BUFFER) += st_magn_buffer.o

obj-$(CONFIG_IIO_ST_MAGN_I2C_3AXIS) += st_magn_i2c.o
obj-$(CONFIG_IIO_ST_MAGN_SPI_3AXIS) += st_magn_spi.o

obj-$(CONFIG_SENSORS_HMC5843)		+= hmc5843_core.o
obj-$(CONFIG_SENSORS_HMC5843_I2C)	+= hmc5843_i2c.o
obj-$(CONFIG_SENSORS_HMC5843_SPI)	+= hmc5843_spi.o

obj-$(CONFIG_SENSORS_RM3100)		+= rm3100-core.o
obj-$(CONFIG_SENSORS_RM3100_I2C)	+= rm3100-i2c.o
obj-$(CONFIG_SENSORS_RM3100_SPI)	+= rm3100-spi.o

obj-$(CONFIG_SI7210)			+= si7210.o

obj-$(CONFIG_TI_TMAG5273)		+= tmag5273.o

obj-$(CONFIG_YAMAHA_YAS530)		+= yamaha-yas530.o
