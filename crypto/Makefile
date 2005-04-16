#
# Cryptographic API
#

proc-crypto-$(CONFIG_PROC_FS) = proc.o

obj-$(CONFIG_CRYPTO) += api.o scatterwalk.o cipher.o digest.o compress.o \
			$(proc-crypto-y)

obj-$(CONFIG_CRYPTO_HMAC) += hmac.o
obj-$(CONFIG_CRYPTO_NULL) += crypto_null.o
obj-$(CONFIG_CRYPTO_MD4) += md4.o
obj-$(CONFIG_CRYPTO_MD5) += md5.o
obj-$(CONFIG_CRYPTO_SHA1) += sha1.o
obj-$(CONFIG_CRYPTO_SHA256) += sha256.o
obj-$(CONFIG_CRYPTO_SHA512) += sha512.o
obj-$(CONFIG_CRYPTO_WP512) += wp512.o
obj-$(CONFIG_CRYPTO_TGR192) += tgr192.o
obj-$(CONFIG_CRYPTO_DES) += des.o
obj-$(CONFIG_CRYPTO_BLOWFISH) += blowfish.o
obj-$(CONFIG_CRYPTO_TWOFISH) += twofish.o
obj-$(CONFIG_CRYPTO_SERPENT) += serpent.o
obj-$(CONFIG_CRYPTO_AES) += aes.o
obj-$(CONFIG_CRYPTO_CAST5) += cast5.o
obj-$(CONFIG_CRYPTO_CAST6) += cast6.o
obj-$(CONFIG_CRYPTO_ARC4) += arc4.o
obj-$(CONFIG_CRYPTO_TEA) += tea.o
obj-$(CONFIG_CRYPTO_KHAZAD) += khazad.o
obj-$(CONFIG_CRYPTO_ANUBIS) += anubis.o
obj-$(CONFIG_CRYPTO_DEFLATE) += deflate.o
obj-$(CONFIG_CRYPTO_MICHAEL_MIC) += michael_mic.o
obj-$(CONFIG_CRYPTO_CRC32C) += crc32c.o

obj-$(CONFIG_CRYPTO_TEST) += tcrypt.o
