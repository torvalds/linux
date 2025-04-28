# SPDX-License-Identifier: GPL-2.0
#
# Arch-specific CryptoAPI modules.
#

obj-$(CONFIG_CRYPTO_AES_ARM) += aes-arm.o
obj-$(CONFIG_CRYPTO_AES_ARM_BS) += aes-arm-bs.o
obj-$(CONFIG_CRYPTO_SHA1_ARM) += sha1-arm.o
obj-$(CONFIG_CRYPTO_SHA1_ARM_NEON) += sha1-arm-neon.o
obj-$(CONFIG_CRYPTO_SHA512_ARM) += sha512-arm.o
obj-$(CONFIG_CRYPTO_BLAKE2B_NEON) += blake2b-neon.o
obj-$(CONFIG_CRYPTO_NHPOLY1305_NEON) += nhpoly1305-neon.o
obj-$(CONFIG_CRYPTO_CURVE25519_NEON) += curve25519-neon.o

obj-$(CONFIG_CRYPTO_AES_ARM_CE) += aes-arm-ce.o
obj-$(CONFIG_CRYPTO_SHA1_ARM_CE) += sha1-arm-ce.o
obj-$(CONFIG_CRYPTO_GHASH_ARM_CE) += ghash-arm-ce.o

aes-arm-y	:= aes-cipher-core.o aes-cipher-glue.o
aes-arm-bs-y	:= aes-neonbs-core.o aes-neonbs-glue.o
sha1-arm-y	:= sha1-armv4-large.o sha1_glue.o
sha1-arm-neon-y	:= sha1-armv7-neon.o sha1_neon_glue.o
sha512-arm-neon-$(CONFIG_KERNEL_MODE_NEON) := sha512-neon-glue.o
sha512-arm-y	:= sha512-core.o sha512-glue.o $(sha512-arm-neon-y)
blake2b-neon-y  := blake2b-neon-core.o blake2b-neon-glue.o
sha1-arm-ce-y	:= sha1-ce-core.o sha1-ce-glue.o
aes-arm-ce-y	:= aes-ce-core.o aes-ce-glue.o
ghash-arm-ce-y	:= ghash-ce-core.o ghash-ce-glue.o
nhpoly1305-neon-y := nh-neon-core.o nhpoly1305-neon-glue.o
curve25519-neon-y := curve25519-core.o curve25519-glue.o

quiet_cmd_perl = PERL    $@
      cmd_perl = $(PERL) $(<) > $(@)

$(obj)/%-core.S: $(src)/%-armv4.pl
	$(call cmd,perl)

clean-files += sha512-core.S

aflags-thumb2-$(CONFIG_THUMB2_KERNEL)  := -U__thumb2__ -D__thumb2__=1

AFLAGS_sha512-core.o += $(aflags-thumb2-y)
