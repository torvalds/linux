# SPDX-License-Identifier: GPL-2.0-only
st_lsm6dsrx-y := st_lsm6dsrx_core.o \
		 st_lsm6dsrx_buffer.o \
		 st_lsm6dsrx_mlc.o \
		 st_lsm6dsrx_shub.o \
		 st_lsm6dsrx_events.o \
		 st_lsm6dsrx_embfunc.o

st_lsm6dsrx-$(CONFIG_IIO_ST_LSM6DSRX_ASYNC_HW_TIMESTAMP) += st_lsm6dsrx_hwtimestamp.o

obj-$(CONFIG_IIO_ST_LSM6DSRX) += st_lsm6dsrx.o
obj-$(CONFIG_IIO_ST_LSM6DSRX_I2C) += st_lsm6dsrx_i2c.o
obj-$(CONFIG_IIO_ST_LSM6DSRX_SPI) += st_lsm6dsrx_spi.o
obj-$(CONFIG_IIO_ST_LSM6DSRX_I3C) += st_lsm6dsrx_i3c.o
