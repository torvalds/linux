# SPDX-License-Identifier: GPL-2.0
#
# Makefile for IAA crypto device drivers
#

ccflags-y += -I $(srctree)/drivers/dma/idxd -DDEFAULT_SYMBOL_NAMESPACE=IDXD

obj-$(CONFIG_CRYPTO_DEV_IAA_CRYPTO) := iaa_crypto.o

iaa_crypto-y := iaa_crypto_main.o iaa_crypto_comp_fixed.o

iaa_crypto-$(CONFIG_CRYPTO_DEV_IAA_CRYPTO_STATS) += iaa_crypto_stats.o
