# SPDX-License-Identifier: GPL-2.0-only
st_lsm6dsox-y := st_lsm6dsox_core.o \
		 st_lsm6dsox_buffer.o \
		 st_lsm6dsox_mlc.o \
		 st_lsm6dsox_shub.o \
		 st_lsm6dsox_embfunc.o

st_lsm6dsox-$(CONFIG_IIO_ST_LSM6DSOX_ASYNC_HW_TIMESTAMP) += st_lsm6dsox_hwtimestamp.o

obj-$(CONFIG_IIO_ST_LSM6DSOX) += st_lsm6dsox.o
obj-$(CONFIG_IIO_ST_LSM6DSOX_I2C) += st_lsm6dsox_i2c.o
obj-$(CONFIG_IIO_ST_LSM6DSOX_SPI) += st_lsm6dsox_spi.o
obj-$(CONFIG_IIO_ST_LSM6DSOX_I3C) += st_lsm6dsox_i3c.o
