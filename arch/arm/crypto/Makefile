# SPDX-License-Identifier: GPL-2.0
#
# Arch-specific CryptoAPI modules.
#

obj-$(CONFIG_CRYPTO_AES_ARM) += aes-arm.o
obj-$(CONFIG_CRYPTO_AES_ARM_BS) += aes-arm-bs.o
obj-$(CONFIG_CRYPTO_BLAKE2B_NEON) += blake2b-neon.o
obj-$(CONFIG_CRYPTO_NHPOLY1305_NEON) += nhpoly1305-neon.o
obj-$(CONFIG_CRYPTO_CURVE25519_NEON) += curve25519-neon.o

obj-$(CONFIG_CRYPTO_AES_ARM_CE) += aes-arm-ce.o
obj-$(CONFIG_CRYPTO_GHASH_ARM_CE) += ghash-arm-ce.o

aes-arm-y	:= aes-cipher-core.o aes-cipher-glue.o
aes-arm-bs-y	:= aes-neonbs-core.o aes-neonbs-glue.o
blake2b-neon-y  := blake2b-neon-core.o blake2b-neon-glue.o
aes-arm-ce-y	:= aes-ce-core.o aes-ce-glue.o
ghash-arm-ce-y	:= ghash-ce-core.o ghash-ce-glue.o
nhpoly1305-neon-y := nh-neon-core.o nhpoly1305-neon-glue.o
curve25519-neon-y := curve25519-core.o curve25519-glue.o
