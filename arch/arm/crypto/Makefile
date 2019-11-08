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
obj-$(CONFIG_CRYPTO_CHACHA20_NEON) += chacha-neon.o
obj-$(CONFIG_CRYPTO_NHPOLY1305_NEON) += nhpoly1305-neon.o

ce-obj-$(CONFIG_CRYPTO_AES_ARM_CE) += aes-arm-ce.o
ce-obj-$(CONFIG_CRYPTO_SHA1_ARM_CE) += sha1-arm-ce.o
ce-obj-$(CONFIG_CRYPTO_SHA2_ARM_CE) += sha2-arm-ce.o
ce-obj-$(CONFIG_CRYPTO_GHASH_ARM_CE) += ghash-arm-ce.o
ce-obj-$(CONFIG_CRYPTO_CRCT10DIF_ARM_CE) += crct10dif-arm-ce.o
crc-obj-$(CONFIG_CRYPTO_CRC32_ARM_CE) += crc32-arm-ce.o

ifneq ($(crc-obj-y)$(crc-obj-m),)
ifeq ($(call as-instr,.arch armv8-a\n.arch_extension crc,y,n),y)
ce-obj-y += $(crc-obj-y)
ce-obj-m += $(crc-obj-m)
else
$(warning These CRC Extensions modules need binutils 2.23 or higher)
$(warning $(crc-obj-y) $(crc-obj-m))
endif
endif

ifneq ($(ce-obj-y)$(ce-obj-m),)
ifeq ($(call as-instr,.fpu crypto-neon-fp-armv8,y,n),y)
obj-y += $(ce-obj-y)
obj-m += $(ce-obj-m)
else
$(warning These ARMv8 Crypto Extensions modules need binutils 2.23 or higher)
$(warning $(ce-obj-y) $(ce-obj-m))
endif
endif

aes-arm-y	:= aes-cipher-core.o aes-cipher-glue.o
aes-arm-bs-y	:= aes-neonbs-core.o aes-neonbs-glue.o
sha1-arm-y	:= sha1-armv4-large.o sha1_glue.o
sha1-arm-neon-y	:= sha1-armv7-neon.o sha1_neon_glue.o
sha256-arm-neon-$(CONFIG_KERNEL_MODE_NEON) := sha256_neon_glue.o
sha256-arm-y	:= sha256-core.o sha256_glue.o $(sha256-arm-neon-y)
sha512-arm-neon-$(CONFIG_KERNEL_MODE_NEON) := sha512-neon-glue.o
sha512-arm-y	:= sha512-core.o sha512-glue.o $(sha512-arm-neon-y)
sha1-arm-ce-y	:= sha1-ce-core.o sha1-ce-glue.o
sha2-arm-ce-y	:= sha2-ce-core.o sha2-ce-glue.o
aes-arm-ce-y	:= aes-ce-core.o aes-ce-glue.o
ghash-arm-ce-y	:= ghash-ce-core.o ghash-ce-glue.o
crct10dif-arm-ce-y	:= crct10dif-ce-core.o crct10dif-ce-glue.o
crc32-arm-ce-y:= crc32-ce-core.o crc32-ce-glue.o
chacha-neon-y := chacha-scalar-core.o chacha-glue.o
chacha-neon-$(CONFIG_KERNEL_MODE_NEON) += chacha-neon-core.o
nhpoly1305-neon-y := nh-neon-core.o nhpoly1305-neon-glue.o

ifdef REGENERATE_ARM_CRYPTO
quiet_cmd_perl = PERL    $@
      cmd_perl = $(PERL) $(<) > $(@)

$(src)/sha256-core.S_shipped: $(src)/sha256-armv4.pl
	$(call cmd,perl)

$(src)/sha512-core.S_shipped: $(src)/sha512-armv4.pl
	$(call cmd,perl)
endif

targets += sha256-core.S sha512-core.S
