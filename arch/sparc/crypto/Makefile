#
# Arch-specific CryptoAPI modules.
#

obj-$(CONFIG_CRYPTO_SHA1_SPARC64) += sha1-sparc64.o
obj-$(CONFIG_CRYPTO_SHA256_SPARC64) += sha256-sparc64.o
obj-$(CONFIG_CRYPTO_SHA512_SPARC64) += sha512-sparc64.o
obj-$(CONFIG_CRYPTO_MD5_SPARC64) += md5-sparc64.o

obj-$(CONFIG_CRYPTO_AES_SPARC64) += aes-sparc64.o
obj-$(CONFIG_CRYPTO_DES_SPARC64) += des-sparc64.o
obj-$(CONFIG_CRYPTO_DES_SPARC64) += camellia-sparc64.o

obj-$(CONFIG_CRYPTO_CRC32C_SPARC64) += crc32c-sparc64.o

sha1-sparc64-y := sha1_asm.o sha1_glue.o
sha256-sparc64-y := sha256_asm.o sha256_glue.o
sha512-sparc64-y := sha512_asm.o sha512_glue.o
md5-sparc64-y := md5_asm.o md5_glue.o

aes-sparc64-y := aes_asm.o aes_glue.o
des-sparc64-y := des_asm.o des_glue.o
camellia-sparc64-y := camellia_asm.o camellia_glue.o

crc32c-sparc64-y := crc32c_asm.o crc32c_glue.o
