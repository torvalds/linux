# SPDX-License-Identifier: GPL-2.0
#
# powerpc/crypto/Makefile
#
# Arch-specific CryptoAPI modules.
#

obj-$(CONFIG_CRYPTO_AES_PPC_SPE) += aes-ppc-spe.o
obj-$(CONFIG_CRYPTO_MD5_PPC) += md5-ppc.o
obj-$(CONFIG_CRYPTO_SHA1_PPC) += sha1-powerpc.o
obj-$(CONFIG_CRYPTO_SHA1_PPC_SPE) += sha1-ppc-spe.o
obj-$(CONFIG_CRYPTO_SHA256_PPC_SPE) += sha256-ppc-spe.o
obj-$(CONFIG_CRYPTO_CRC32C_VPMSUM) += crc32c-vpmsum.o
obj-$(CONFIG_CRYPTO_CRCT10DIF_VPMSUM) += crct10dif-vpmsum.o
obj-$(CONFIG_CRYPTO_VPMSUM_TESTER) += crc-vpmsum_test.o

aes-ppc-spe-y := aes-spe-core.o aes-spe-keys.o aes-tab-4k.o aes-spe-modes.o aes-spe-glue.o
md5-ppc-y := md5-asm.o md5-glue.o
sha1-powerpc-y := sha1-powerpc-asm.o sha1.o
sha1-ppc-spe-y := sha1-spe-asm.o sha1-spe-glue.o
sha256-ppc-spe-y := sha256-spe-asm.o sha256-spe-glue.o
crc32c-vpmsum-y := crc32c-vpmsum_asm.o crc32c-vpmsum_glue.o
crct10dif-vpmsum-y := crct10dif-vpmsum_asm.o crct10dif-vpmsum_glue.o
