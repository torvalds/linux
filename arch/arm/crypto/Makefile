#
# Arch-specific CryptoAPI modules.
#

obj-$(CONFIG_CRYPTO_AES_ARM) += aes-arm.o
obj-$(CONFIG_CRYPTO_AES_ARM_BS) += aes-arm-bs.o
obj-$(CONFIG_CRYPTO_SHA1_ARM) += sha1-arm.o
obj-$(CONFIG_CRYPTO_SHA1_ARM_NEON) += sha1-arm-neon.o
obj-$(CONFIG_CRYPTO_SHA256_ARM) += sha256-arm.o
obj-$(CONFIG_CRYPTO_SHA512_ARM_NEON) += sha512-arm-neon.o

ce-obj-$(CONFIG_CRYPTO_AES_ARM_CE) += aes-arm-ce.o
ce-obj-$(CONFIG_CRYPTO_SHA1_ARM_CE) += sha1-arm-ce.o
ce-obj-$(CONFIG_CRYPTO_SHA2_ARM_CE) += sha2-arm-ce.o
ce-obj-$(CONFIG_CRYPTO_GHASH_ARM_CE) += ghash-arm-ce.o

ifneq ($(ce-obj-y)$(ce-obj-m),)
ifeq ($(call as-instr,.fpu crypto-neon-fp-armv8,y,n),y)
obj-y += $(ce-obj-y)
obj-m += $(ce-obj-m)
else
$(warning These ARMv8 Crypto Extensions modules need binutils 2.23 or higher)
$(warning $(ce-obj-y) $(ce-obj-m))
endif
endif

aes-arm-y	:= aes-armv4.o aes_glue.o
aes-arm-bs-y	:= aesbs-core.o aesbs-glue.o
sha1-arm-y	:= sha1-armv4-large.o sha1_glue.o
sha1-arm-neon-y	:= sha1-armv7-neon.o sha1_neon_glue.o
sha256-arm-neon-$(CONFIG_KERNEL_MODE_NEON) := sha256_neon_glue.o
sha256-arm-y	:= sha256-core.o sha256_glue.o $(sha256-arm-neon-y)
sha512-arm-neon-y := sha512-armv7-neon.o sha512_neon_glue.o
sha1-arm-ce-y	:= sha1-ce-core.o sha1-ce-glue.o
sha2-arm-ce-y	:= sha2-ce-core.o sha2-ce-glue.o
aes-arm-ce-y	:= aes-ce-core.o aes-ce-glue.o
ghash-arm-ce-y	:= ghash-ce-core.o ghash-ce-glue.o

quiet_cmd_perl = PERL    $@
      cmd_perl = $(PERL) $(<) > $(@)

$(src)/aesbs-core.S_shipped: $(src)/bsaes-armv7.pl
	$(call cmd,perl)

$(src)/sha256-core.S_shipped: $(src)/sha256-armv4.pl
	$(call cmd,perl)

.PRECIOUS: $(obj)/aesbs-core.S $(obj)/sha256-core.S
