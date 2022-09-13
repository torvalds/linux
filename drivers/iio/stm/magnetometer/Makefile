# SPDX-License-Identifier: GPL-2.0
#
# Makefile for industrial I/O Magnetometer sensor drivers
#

# When adding new entries keep the list in alphabetical order
st_mag3d-y += st_mag3d_core.o st_mag3d_buffer.o
obj-$(CONFIG_ST_MAG3D_IIO) += st_mag3d.o
obj-$(CONFIG_ST_MAG3D_I2C_IIO) += st_mag3d_i2c.o
obj-$(CONFIG_ST_MAG3D_SPI_IIO) += st_mag3d_spi.o

st_mag40-y += st_mag40_buffer.o st_mag40_core.o
obj-$(CONFIG_ST_MAG40_IIO) += st_mag40.o
obj-$(CONFIG_ST_MAG40_I2C_IIO) += st_mag40_i2c.o
obj-$(CONFIG_ST_MAG40_SPI_IIO) += st_mag40_spi.o
