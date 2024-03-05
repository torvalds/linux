# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for STMicroelectronics LSM6DS3 sensor.
#

obj-$(CONFIG_ST_LSM6DS3_IIO) += st_lsm6ds3.o
st_lsm6ds3-objs := st_lsm6ds3_core.o
obj-$(CONFIG_ST_LSM6DS3_I2C_IIO) += st_lsm6ds3_i2c.o
obj-$(CONFIG_ST_LSM6DS3_SPI_IIO) += st_lsm6ds3_spi.o

st_lsm6ds3-$(CONFIG_IIO_BUFFER) += st_lsm6ds3_buffer.o
st_lsm6ds3-$(CONFIG_IIO_TRIGGER) += st_lsm6ds3_trigger.o
st_lsm6ds3-$(CONFIG_ST_LSM6DS3_IIO_MASTER_SUPPORT) += st_lsm6ds3_i2c_master.o
