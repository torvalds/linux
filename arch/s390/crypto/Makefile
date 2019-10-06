# SPDX-License-Identifier: GPL-2.0
#
# Cryptographic API
#

obj-$(CONFIG_CRYPTO_SHA1_S390) += sha1_s390.o sha_common.o
obj-$(CONFIG_CRYPTO_SHA256_S390) += sha256_s390.o sha_common.o
obj-$(CONFIG_CRYPTO_SHA512_S390) += sha512_s390.o sha_common.o
obj-$(CONFIG_CRYPTO_SHA3_256_S390) += sha3_256_s390.o sha_common.o
obj-$(CONFIG_CRYPTO_SHA3_512_S390) += sha3_512_s390.o sha_common.o
obj-$(CONFIG_CRYPTO_DES_S390) += des_s390.o
obj-$(CONFIG_CRYPTO_AES_S390) += aes_s390.o
obj-$(CONFIG_CRYPTO_PAES_S390) += paes_s390.o
obj-$(CONFIG_S390_PRNG) += prng.o
obj-$(CONFIG_CRYPTO_GHASH_S390) += ghash_s390.o
obj-$(CONFIG_CRYPTO_CRC32_S390) += crc32-vx_s390.o
obj-$(CONFIG_ARCH_RANDOM) += arch_random.o

crc32-vx_s390-y := crc32-vx.o crc32le-vx.o crc32be-vx.o
