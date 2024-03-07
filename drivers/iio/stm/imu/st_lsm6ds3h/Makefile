# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for STMicroelectronics LSM6DS3H sensor.
#

obj-$(CONFIG_ST_LSM6DS3H_IIO) += st_lsm6ds3h.o
st_lsm6ds3h-objs := st_lsm6ds3h_core.o
obj-$(CONFIG_ST_LSM6DS3H_I2C_IIO) += st_lsm6ds3h_i2c.o
obj-$(CONFIG_ST_LSM6DS3H_SPI_IIO) += st_lsm6ds3h_spi.o

st_lsm6ds3h-$(CONFIG_IIO_BUFFER) += st_lsm6ds3h_buffer.o
st_lsm6ds3h-$(CONFIG_IIO_TRIGGER) += st_lsm6ds3h_trigger.o
st_lsm6ds3h-$(CONFIG_ST_LSM6DS3H_IIO_MASTER_SUPPORT) += st_lsm6ds3h_i2c_master.o
