# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_CRYPTO_DEV_SPACC) += snps-spacc.o
snps-spacc-objs = spacc_hal.o spacc_core.o \
spacc_manager.o spacc_interrupt.o spacc_device.o

ifeq ($(CONFIG_CRYPTO_DEV_SPACC_HASH),y)
snps-spacc-objs += spacc_ahash.o
endif

ifeq ($(CONFIG_CRYPTO_DEV_SPACC_CIPHER),y)
snps-spacc-objs += spacc_skcipher.o
endif

ifeq ($(CONFIG_CRYPTO_DEV_SPACC_AEAD),y)
snps-spacc-objs += spacc_aead.o
endif
