# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_CRYPTO_DEV_ROCKCHIP) += rk_crypto.o
rk_crypto-objs := rk_crypto_core.o \
		  rk_crypto_utils.o \
		  rk_crypto_ahash_utils.o \
		  rk_crypto_skcipher_utils.o \
		  procfs.o

rk_crypto-$(CONFIG_CRYPTO_DEV_ROCKCHIP_V1) += \
		rk_crypto_v1.o \
		rk_crypto_v1_ahash.o \
		rk_crypto_v1_skcipher.o

rk_crypto-$(CONFIG_CRYPTO_DEV_ROCKCHIP_V2) += \
		rk_crypto_v2.o \
		rk_crypto_v2_ahash.o \
		rk_crypto_v2_skcipher.o \
		rk_crypto_v2_akcipher.o \
		rk_crypto_v2_pka.o \
		rk_crypto_bignum.o

rk_crypto-$(CONFIG_CRYPTO_DEV_ROCKCHIP_V3) += \
		rk_crypto_v3.o \
		rk_crypto_v3_ahash.o \
		rk_crypto_v3_skcipher.o \
		rk_crypto_v2_akcipher.o \
		rk_crypto_v2_pka.o \
		rk_crypto_bignum.o

obj-$(CONFIG_CRYPTO_DEV_ROCKCHIP_DEV) += cryptodev_linux/
