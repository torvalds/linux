# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for STMicroelectronics ISM330DLC sensor.
#

obj-$(CONFIG_ST_ISM330DLC_IIO) += st_ism330dlc.o
st_ism330dlc-objs := st_ism330dlc_core.o
obj-$(CONFIG_ST_ISM330DLC_I2C_IIO) += st_ism330dlc_i2c.o
obj-$(CONFIG_ST_ISM330DLC_SPI_IIO) += st_ism330dlc_spi.o

st_ism330dlc-$(CONFIG_IIO_BUFFER) += st_ism330dlc_buffer.o
st_ism330dlc-$(CONFIG_IIO_TRIGGER) += st_ism330dlc_trigger.o
st_ism330dlc-$(CONFIG_ST_ISM330DLC_IIO_MASTER_SUPPORT) += st_ism330dlc_i2c_master.o
