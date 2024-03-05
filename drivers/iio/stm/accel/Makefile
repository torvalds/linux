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

ism303dac_accel-y += st_ism303dac_accel_core.o st_ism303dac_accel_buffer.o \
	st_ism303dac_accel_trigger.o
obj-$(CONFIG_IIO_ST_ISM303DAC_ACCEL) += ism303dac_accel.o
obj-$(CONFIG_IIO_ST_ISM303DAC_ACCEL_I2C) += st_ism303dac_accel_i2c.o
obj-$(CONFIG_IIO_ST_ISM303DAC_ACCEL_SPI) += st_ism303dac_accel_spi.o

st-lis3dhh-y := st_lis3dhh.o st_lis3dhh_buffer.o
obj-$(CONFIG_IIO_ST_LIS3DHH) += st-lis3dhh.o

lis2hh12-y += st_lis2hh12_core.o st_lis2hh12_buffer.o st_lis2hh12_trigger.o
obj-$(CONFIG_IIO_ST_LIS2HH12) += lis2hh12.o
obj-$(CONFIG_IIO_ST_LIS2HH12_I2C) += st_lis2hh12_i2c.o
obj-$(CONFIG_IIO_ST_LIS2HH12_SPI) += st_lis2hh12_spi.o

st_lis2duxs12-y := st_lis2duxs12_core.o st_lis2duxs12_buffer.o \
                  st_lis2duxs12_mlc.o st_lis2duxs12_embfunc.o \
		  st_lis2duxs12_basicfunc.o st_lis2duxs12_qvar.o

obj-$(CONFIG_IIO_ST_LIS2DUXS12) += st_lis2duxs12.o
obj-$(CONFIG_IIO_ST_LIS2DUXS12_I2C) += st_lis2duxs12_i2c.o
obj-$(CONFIG_IIO_ST_LIS2DUXS12_SPI) += st_lis2duxs12_spi.o
obj-$(CONFIG_IIO_ST_LIS2DUXS12_I3C) += st_lis2duxs12_i3c.o

st_h3lis331dl-y := st_h3lis331dl_core.o st_h3lis331dl_buffer.o

obj-$(CONFIG_IIO_ST_H3LIS331DL) += st_h3lis331dl.o
obj-$(CONFIG_IIO_ST_H3LIS331DL_I2C) += st_h3lis331dl_i2c.o
obj-$(CONFIG_IIO_ST_H3LIS331DL_SPI) += st_h3lis331dl_spi.o

st_lis2du12-y:= st_lis2du12_core.o st_lis2du12_buffer.o
obj-$(CONFIG_IIO_ST_LIS2DU12) += st_lis2du12.o
obj-$(CONFIG_IIO_ST_LIS2DU12_I2C) += st_lis2du12_i2c.o
obj-$(CONFIG_IIO_ST_LIS2DU12_SPI) += st_lis2du12_spi.o
obj-$(CONFIG_IIO_ST_LIS2DU12_I3C) += st_lis2du12_i3c.o

