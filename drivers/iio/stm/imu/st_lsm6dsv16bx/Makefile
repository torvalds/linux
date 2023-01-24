# SPDX-License-Identifier: GPL-2.0-only
st_lsm6dsv16bx-y := st_lsm6dsv16bx_core.o st_lsm6dsv16bx_buffer.o \
		 st_lsm6dsv16bx_qvar.o st_lsm6dsv16bx_mlc.o \
		 st_lsm6dsv16bx_events.o st_lsm6dsv16bx_embfunc.o

st_lsm6dsv16bx-$(CONFIG_IIO_ST_LSM6DSV16BX_ASYNC_HW_TIMESTAMP) += st_lsm6dsv16bx_hwtimestamp.o

obj-$(CONFIG_IIO_ST_LSM6DSV16BX) += st_lsm6dsv16bx.o
obj-$(CONFIG_IIO_ST_LSM6DSV16BX_I2C) += st_lsm6dsv16bx_i2c.o
obj-$(CONFIG_IIO_ST_LSM6DSV16BX_SPI) += st_lsm6dsv16bx_spi.o
obj-$(CONFIG_IIO_ST_LSM6DSV16BX_I3C) += st_lsm6dsv16bx_i3c.o
