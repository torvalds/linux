# SPDX-License-Identifier: GPL-2.0
#
# Makefile for industrial I/O accelerometer drivers
#

# When adding new entries keep the list in alphabetical order
obj-$(CONFIG_IIO_ST_LIS2DS12) += lis2ds12.o
obj-$(CONFIG_IIO_ST_LIS2DS12_I2C) += st_lis2ds12_i2c.o
obj-$(CONFIG_IIO_ST_LIS2DS12_SPI) += st_lis2ds12_spi.o

lis2ds12-y += st_lis2ds12_core.o st_lis2ds12_buffer.o st_lis2ds12_trigger.o

st_acc33-y := st_acc33_core.o st_acc33_buffer.o
obj-$(CONFIG_IIO_ST_ACC33) += st_acc33.o
obj-$(CONFIG_IIO_ST_ACC33_I2C) += st_acc33_i2c.o
obj-$(CONFIG_IIO_ST_ACC33_SPI) += st_acc33_spi.o

st_lis2dw12-y:= st_lis2dw12_core.o st_lis2dw12_buffer.o
obj-$(CONFIG_IIO_ST_LIS2DW12) += st_lis2dw12.o
obj-$(CONFIG_IIO_ST_LIS2DW12_I2C) += st_lis2dw12_i2c.o
obj-$(CONFIG_IIO_ST_LIS2DW12_SPI) += st_lis2dw12_spi.o
