# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Invensense MPU6050 device.
#

obj-$(CONFIG_INV_MPU6050_IIO) += inv-mpu6050.o
inv-mpu6050-y := inv_mpu_core.o inv_mpu_ring.o inv_mpu_trigger.o \
		 inv_mpu_aux.o inv_mpu_magn.o

obj-$(CONFIG_INV_MPU6050_I2C) += inv-mpu6050-i2c.o
inv-mpu6050-i2c-y := inv_mpu_i2c.o inv_mpu_acpi.o

obj-$(CONFIG_INV_MPU6050_SPI) += inv-mpu6050-spi.o
inv-mpu6050-spi-y := inv_mpu_spi.o
