# SPDX-License-Identifier: GPL-2.0
#
# Arch-specific CryptoAPI modules.
#

obj-$(CONFIG_CRYPTO_AES_ARM) += aes-arm.o
obj-$(CONFIG_CRYPTO_AES_ARM_BS) += aes-arm-bs.o
obj-$(CONFIG_CRYPTO_SHA1_ARM) += sha1-arm.o
obj-$(CONFIG_CRYPTO_SHA1_ARM_NEON) += sha1-arm-neon.o
obj-$(CONFIG_CRYPTO_SHA256_ARM) += sha256-arm.o
obj-$(CONFIG_CRYPTO_SHA512_ARM) += sha512-arm.o
obj-$(CONFIG_CRYPTO_BLAKE2S_ARM) += libblake2s-arm.o
obj-$(CONFIG_CRYPTO_BLAKE2B_NEON) += blake2b-neon.o
obj-$(CONFIG_CRYPTO_CHACHA20_NEON) += chacha-neon.o
obj-$(CONFIG_CRYPTO_POLY1305_ARM) += poly1305-arm.o
obj-$(CONFIG_CRYPTO_NHPOLY1305_NEON) += nhpoly1305-neon.o
obj-$(CONFIG_CRYPTO_CURVE25519_NEON) += curve25519-neon.o

obj-$(CONFIG_CRYPTO_AES_ARM_CE) += aes-arm-ce.o
obj-$(CONFIG_CRYPTO_SHA1_ARM_CE) += sha1-arm-ce.o
obj-$(CONFIG_CRYPTO_SHA2_ARM_CE) += sha2-arm-ce.o
obj-$(CONFIG_CRYPTO_GHASH_ARM_CE) += ghash-arm-ce.o
obj-$(CONFIG_CRYPTO_CRCT10DIF_ARM_CE) += crct10dif-arm-ce.o
obj-$(CONFIG_CRYPTO_CRC32_ARM_CE) += crc32-arm-ce.o

aes-arm-y	:= aes-cipher-core.o aes-cipher-glue.o
aes-arm-bs-y	:= aes-neonbs-core.o aes-neonbs-glue.o
sha1-arm-y	:= sha1-armv4-large.o sha1_glue.o
sha1-arm-neon-y	:= sha1-armv7-neon.o sha1_neon_glue.o
sha256-arm-neon-$(CONFIG_KERNEL_MODE_NEON) := sha256_neon_glue.o
sha256-arm-y	:= sha256-core.o sha256_glue.o $(sha256-arm-neon-y)
sha512-arm-neon-$(CONFIG_KERNEL_MODE_NEON) := sha512-neon-glue.o
sha512-arm-y	:= sha512-core.o sha512-glue.o $(sha512-arm-neon-y)
libblake2s-arm-y:= blake2s-core.o blake2s-glue.o
blake2b-neon-y  := blake2b-neon-core.o blake2b-neon-glue.o
sha1-arm-ce-y	:= sha1-ce-core.o sha1-ce-glue.o
sha2-arm-ce-y	:= sha2-ce-core.o sha2-ce-glue.o
aes-arm-ce-y	:= aes-ce-core.o aes-ce-glue.o
ghash-arm-ce-y	:= ghash-ce-core.o ghash-ce-glue.o
crct10dif-arm-ce-y	:= crct10dif-ce-core.o crct10dif-ce-glue.o
crc32-arm-ce-y:= crc32-ce-core.o crc32-ce-glue.o
chacha-neon-y := chacha-scalar-core.o chacha-glue.o
chacha-neon-$(CONFIG_KERNEL_MODE_NEON) += chacha-neon-core.o
poly1305-arm-y := poly1305-core.o poly1305-glue.o
nhpoly1305-neon-y := nh-neon-core.o nhpoly1305-neon-glue.o
curve25519-neon-y := curve25519-core.o curve25519-glue.o

quiet_cmd_perl = PERL    $@
      cmd_perl = $(PERL) $(<) > $(@)

$(obj)/%-core.S: $(src)/%-armv4.pl
	$(call cmd,perl)

clean-files += poly1305-core.S sha256-core.S sha512-core.S

# massage the perlasm code a bit so we only get the NEON routine if we need it
poly1305-aflags-$(CONFIG_CPU_V7) := -U__LINUX_ARM_ARCH__ -D__LINUX_ARM_ARCH__=5
poly1305-aflags-$(CONFIG_KERNEL_MODE_NEON) := -U__LINUX_ARM_ARCH__ -D__LINUX_ARM_ARCH__=7
AFLAGS_poly1305-core.o += $(poly1305-aflags-y)
