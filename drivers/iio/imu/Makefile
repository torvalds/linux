# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Inertial Measurement Units
#

# When adding new entries keep the list in alphabetical order
obj-$(CONFIG_ADIS16400) += adis16400.o
obj-$(CONFIG_ADIS16460) += adis16460.o
obj-$(CONFIG_ADIS16475) += adis16475.o
obj-$(CONFIG_ADIS16480) += adis16480.o
obj-$(CONFIG_ADIS16550) += adis16550.o

adis_lib-y += adis.o
adis_lib-$(CONFIG_IIO_ADIS_LIB_BUFFER) += adis_trigger.o
adis_lib-$(CONFIG_IIO_ADIS_LIB_BUFFER) += adis_buffer.o
obj-$(CONFIG_IIO_ADIS_LIB) += adis_lib.o

obj-y += bmi160/
obj-y += bmi270/
obj-y += bmi323/
obj-y += bno055/

obj-$(CONFIG_FXOS8700) += fxos8700_core.o
obj-$(CONFIG_FXOS8700_I2C) += fxos8700_i2c.o
obj-$(CONFIG_FXOS8700_SPI) += fxos8700_spi.o

obj-y += inv_icm42600/
obj-y += inv_mpu6050/

obj-$(CONFIG_KMX61) += kmx61.o

obj-$(CONFIG_SMI240) += smi240.o

obj-y += st_lsm6dsx/
obj-y += st_lsm9ds0/
