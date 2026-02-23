# SPDX-License-Identifier: GPL-2.0
#
# Arch-specific CryptoAPI modules.
#

obj-$(CONFIG_CRYPTO_AES_ARM_BS) += aes-arm-bs.o

obj-$(CONFIG_CRYPTO_AES_ARM_CE) += aes-arm-ce.o
obj-$(CONFIG_CRYPTO_GHASH_ARM_CE) += ghash-arm-ce.o

aes-arm-bs-y	:= aes-neonbs-core.o aes-neonbs-glue.o
aes-arm-ce-y	:= aes-ce-core.o aes-ce-glue.o
ghash-arm-ce-y	:= ghash-ce-core.o ghash-ce-glue.o
