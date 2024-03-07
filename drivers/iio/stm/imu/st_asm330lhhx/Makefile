# SPDX-License-Identifier: GPL-2.0-only
st_asm330lhhx-y := st_asm330lhhx_core.o st_asm330lhhx_buffer.o \
		  st_asm330lhhx_events.o \
		  st_asm330lhhx_shub.o \
		  st_asm330lhhx_mlc.o

st_asm330lhhx-$(CONFIG_IIO_ST_ASM330LHHX_ASYNC_HW_TIMESTAMP) += st_asm330lhhx_hwtimestamp.o

obj-$(CONFIG_IIO_ST_ASM330LHHX) += st_asm330lhhx.o
obj-$(CONFIG_IIO_ST_ASM330LHHX_I2C) += st_asm330lhhx_i2c.o
obj-$(CONFIG_IIO_ST_ASM330LHHX_SPI) += st_asm330lhhx_spi.o
