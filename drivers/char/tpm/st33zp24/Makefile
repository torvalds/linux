# SPDX-License-Identifier: GPL-2.0
#
# Makefile for ST33ZP24 TPM 1.2 driver
#

tpm_st33zp24-objs = st33zp24.o
obj-$(CONFIG_TCG_TIS_ST33ZP24) += tpm_st33zp24.o

tpm_st33zp24_i2c-objs = i2c.o
obj-$(CONFIG_TCG_TIS_ST33ZP24_I2C) += tpm_st33zp24_i2c.o

tpm_st33zp24_spi-objs = spi.o
obj-$(CONFIG_TCG_TIS_ST33ZP24_SPI) += tpm_st33zp24_spi.o
