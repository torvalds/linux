# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for STMicroelectronics LSM6DSM sensor.
#

obj-$(CONFIG_ST_LSM6DSM_IIO) += st_lsm6dsm.o
st_lsm6dsm-objs := st_lsm6dsm_core.o
obj-$(CONFIG_ST_LSM6DSM_I2C_IIO) += st_lsm6dsm_i2c.o
obj-$(CONFIG_ST_LSM6DSM_SPI_IIO) += st_lsm6dsm_spi.o

st_lsm6dsm-$(CONFIG_IIO_BUFFER) += st_lsm6dsm_buffer.o
st_lsm6dsm-$(CONFIG_IIO_TRIGGER) += st_lsm6dsm_trigger.o
st_lsm6dsm-$(CONFIG_ST_LSM6DSM_IIO_MASTER_SUPPORT) += st_lsm6dsm_i2c_master.o
