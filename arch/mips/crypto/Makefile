# SPDX-License-Identifier: GPL-2.0
#
# Makefile for MIPS crypto files..
#

obj-$(CONFIG_CRYPTO_CRC32_MIPS) += crc32-mips.o

obj-$(CONFIG_CRYPTO_CHACHA_MIPS) += chacha-mips.o
chacha-mips-y := chacha-core.o chacha-glue.o
AFLAGS_chacha-core.o += -O2 # needed to fill branch delay slots
