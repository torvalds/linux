# SPDX-License-Identifier: GPL-2.0
#
# Arch-specific CryptoAPI modules.
#

obj-$(CONFIG_CRYPTO_MD5_SPARC64) += md5-sparc64.o

obj-$(CONFIG_CRYPTO_AES_SPARC64) += aes-sparc64.o
obj-$(CONFIG_CRYPTO_DES_SPARC64) += des-sparc64.o
obj-$(CONFIG_CRYPTO_CAMELLIA_SPARC64) += camellia-sparc64.o

md5-sparc64-y := md5_asm.o md5_glue.o

aes-sparc64-y := aes_asm.o aes_glue.o
des-sparc64-y := des_asm.o des_glue.o
camellia-sparc64-y := camellia_asm.o camellia_glue.o
