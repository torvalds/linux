#
# Cryptographic API
#

obj-$(CONFIG_CRYPTO) += crypto.o
crypto-objs := api.o cipher.o compress.o

obj-$(CONFIG_CRYPTO_WORKQUEUE) += crypto_wq.o

obj-$(CONFIG_CRYPTO_FIPS) += fips.o

crypto_algapi-$(CONFIG_PROC_FS) += proc.o
crypto_algapi-objs := algapi.o scatterwalk.o $(crypto_algapi-y)
obj-$(CONFIG_CRYPTO_ALGAPI2) += crypto_algapi.o

obj-$(CONFIG_CRYPTO_AEAD2) += aead.o

crypto_blkcipher-objs := ablkcipher.o
crypto_blkcipher-objs += blkcipher.o
obj-$(CONFIG_CRYPTO_BLKCIPHER2) += crypto_blkcipher.o
obj-$(CONFIG_CRYPTO_BLKCIPHER2) += chainiv.o
obj-$(CONFIG_CRYPTO_BLKCIPHER2) += eseqiv.o
obj-$(CONFIG_CRYPTO_SEQIV) += seqiv.o

crypto_hash-objs += ahash.o
crypto_hash-objs += shash.o
obj-$(CONFIG_CRYPTO_HASH2) += crypto_hash.o

obj-$(CONFIG_CRYPTO_PCOMP) += pcompress.o

cryptomgr-objs := algboss.o testmgr.o

obj-$(CONFIG_CRYPTO_MANAGER2) += cryptomgr.o
obj-$(CONFIG_CRYPTO_HMAC) += hmac.o
obj-$(CONFIG_CRYPTO_VMAC) += vmac.o
obj-$(CONFIG_CRYPTO_XCBC) += xcbc.o
obj-$(CONFIG_CRYPTO_NULL) += crypto_null.o
obj-$(CONFIG_CRYPTO_MD4) += md4.o
obj-$(CONFIG_CRYPTO_MD5) += md5.o
obj-$(CONFIG_CRYPTO_RMD128) += rmd128.o
obj-$(CONFIG_CRYPTO_RMD160) += rmd160.o
obj-$(CONFIG_CRYPTO_RMD256) += rmd256.o
obj-$(CONFIG_CRYPTO_RMD320) += rmd320.o
obj-$(CONFIG_CRYPTO_SHA1) += sha1_generic.o
obj-$(CONFIG_CRYPTO_SHA256) += sha256_generic.o
obj-$(CONFIG_CRYPTO_SHA512) += sha512_generic.o
obj-$(CONFIG_CRYPTO_WP512) += wp512.o
obj-$(CONFIG_CRYPTO_TGR192) += tgr192.o
obj-$(CONFIG_CRYPTO_GF128MUL) += gf128mul.o
obj-$(CONFIG_CRYPTO_ECB) += ecb.o
obj-$(CONFIG_CRYPTO_CBC) += cbc.o
obj-$(CONFIG_CRYPTO_PCBC) += pcbc.o
obj-$(CONFIG_CRYPTO_CTS) += cts.o
obj-$(CONFIG_CRYPTO_LRW) += lrw.o
obj-$(CONFIG_CRYPTO_XTS) += xts.o
obj-$(CONFIG_CRYPTO_CTR) += ctr.o
obj-$(CONFIG_CRYPTO_GCM) += gcm.o
obj-$(CONFIG_CRYPTO_CCM) += ccm.o
obj-$(CONFIG_CRYPTO_PCRYPT) += pcrypt.o
obj-$(CONFIG_CRYPTO_CRYPTD) += cryptd.o
obj-$(CONFIG_CRYPTO_DES) += des_generic.o
obj-$(CONFIG_CRYPTO_FCRYPT) += fcrypt.o
obj-$(CONFIG_CRYPTO_BLOWFISH) += blowfish.o
obj-$(CONFIG_CRYPTO_TWOFISH) += twofish.o
obj-$(CONFIG_CRYPTO_TWOFISH_COMMON) += twofish_common.o
obj-$(CONFIG_CRYPTO_SERPENT) += serpent.o
obj-$(CONFIG_CRYPTO_AES) += aes_generic.o
obj-$(CONFIG_CRYPTO_CAMELLIA) += camellia.o
obj-$(CONFIG_CRYPTO_CAST5) += cast5.o
obj-$(CONFIG_CRYPTO_CAST6) += cast6.o
obj-$(CONFIG_CRYPTO_ARC4) += arc4.o
obj-$(CONFIG_CRYPTO_TEA) += tea.o
obj-$(CONFIG_CRYPTO_KHAZAD) += khazad.o
obj-$(CONFIG_CRYPTO_ANUBIS) += anubis.o
obj-$(CONFIG_CRYPTO_SEED) += seed.o
obj-$(CONFIG_CRYPTO_SALSA20) += salsa20_generic.o
obj-$(CONFIG_CRYPTO_DEFLATE) += deflate.o
obj-$(CONFIG_CRYPTO_ZLIB) += zlib.o
obj-$(CONFIG_CRYPTO_MICHAEL_MIC) += michael_mic.o
obj-$(CONFIG_CRYPTO_CRC32C) += crc32c.o
obj-$(CONFIG_CRYPTO_AUTHENC) += authenc.o
obj-$(CONFIG_CRYPTO_LZO) += lzo.o
obj-$(CONFIG_CRYPTO_RNG2) += rng.o
obj-$(CONFIG_CRYPTO_RNG2) += krng.o
obj-$(CONFIG_CRYPTO_ANSI_CPRNG) += ansi_cprng.o
obj-$(CONFIG_CRYPTO_TEST) += tcrypt.o
obj-$(CONFIG_CRYPTO_GHASH) += ghash-generic.o

#
# generic algorithms and the async_tx api
#
obj-$(CONFIG_XOR_BLOCKS) += xor.o
obj-$(CONFIG_ASYNC_CORE) += async_tx/
