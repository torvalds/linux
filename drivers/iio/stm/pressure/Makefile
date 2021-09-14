# SPDX-License-Identifier: GPL-2.0
#
# Makefile for industrial I/O pressure drivers
#

# When adding new entries keep the list in alphabetical order
obj-$(CONFIG_ST_LPS22HH_IIO) += st_lps22hh.o
obj-$(CONFIG_ST_LPS22HH_I2C_IIO) += st_lps22hh_i2c.o
obj-$(CONFIG_ST_LPS22HH_SPI_IIO) += st_lps22hh_spi.o

st_lps22hh-y += st_lps22hh_core.o st_lps22hh_buffer.o

obj-$(CONFIG_ST_LPS22DF_IIO) += st_lps22df.o
obj-$(CONFIG_ST_LPS22DF_I2C_IIO) += st_lps22df_i2c.o
obj-$(CONFIG_ST_LPS22DF_SPI_IIO) += st_lps22df_spi.o

st_lps22df-y += st_lps22df_core.o st_lps22df_buffer.o

