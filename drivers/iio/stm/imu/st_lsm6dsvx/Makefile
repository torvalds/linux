# SPDX-License-Identifier: GPL-2.0-only
st_lsm6dsvx-y := st_lsm6dsvx_core.o st_lsm6dsvx_buffer.o \
		 st_lsm6dsvx_shub.o st_lsm6dsvx_qvar.o \
		 st_lsm6dsvx_mlc.o st_lsm6dsvx_events.o \
		 st_lsm6dsvx_embfunc.o

st_lsm6dsvx-$(CONFIG_IIO_ST_LSM6DSVX_ASYNC_HW_TIMESTAMP) += st_lsm6dsvx_hwtimestamp.o

obj-$(CONFIG_IIO_ST_LSM6DSVX) += st_lsm6dsvx.o
obj-$(CONFIG_IIO_ST_LSM6DSVX_I2C) += st_lsm6dsvx_i2c.o
obj-$(CONFIG_IIO_ST_LSM6DSVX_SPI) += st_lsm6dsvx_spi.o
obj-$(CONFIG_IIO_ST_LSM6DSVX_I3C) += st_lsm6dsvx_i3c.o
