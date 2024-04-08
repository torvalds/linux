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
obj-$(CONFIG_CRYPTO_AES_GCM_P10) += aes-gcm-p10-crypto.o
obj-$(CONFIG_CRYPTO_CHACHA20_P10) += chacha-p10-crypto.o
obj-$(CONFIG_CRYPTO_POLY1305_P10) += poly1305-p10-crypto.o
obj-$(CONFIG_CRYPTO_DEV_VMX_ENCRYPT) += vmx-crypto.o

aes-ppc-spe-y := aes-spe-core.o aes-spe-keys.o aes-tab-4k.o aes-spe-modes.o aes-spe-glue.o
md5-ppc-y := md5-asm.o md5-glue.o
sha1-powerpc-y := sha1-powerpc-asm.o sha1.o
sha1-ppc-spe-y := sha1-spe-asm.o sha1-spe-glue.o
sha256-ppc-spe-y := sha256-spe-asm.o sha256-spe-glue.o
crc32c-vpmsum-y := crc32c-vpmsum_asm.o crc32c-vpmsum_glue.o
crct10dif-vpmsum-y := crct10dif-vpmsum_asm.o crct10dif-vpmsum_glue.o
aes-gcm-p10-crypto-y := aes-gcm-p10-glue.o aes-gcm-p10.o ghashp10-ppc.o aesp10-ppc.o
chacha-p10-crypto-y := chacha-p10-glue.o chacha-p10le-8x.o
poly1305-p10-crypto-y := poly1305-p10-glue.o poly1305-p10le_64.o
vmx-crypto-objs := vmx.o aesp8-ppc.o ghashp8-ppc.o aes.o aes_cbc.o aes_ctr.o aes_xts.o ghash.o

ifeq ($(CONFIG_CPU_LITTLE_ENDIAN),y)
override flavour := linux-ppc64le
else
ifdef CONFIG_PPC64_ELF_ABI_V2
override flavour := linux-ppc64-elfv2
else
override flavour := linux-ppc64
endif
endif

quiet_cmd_perl = PERL    $@
      cmd_perl = $(PERL) $< $(flavour) > $@

targets += aesp10-ppc.S ghashp10-ppc.S aesp8-ppc.S ghashp8-ppc.S

$(obj)/aesp10-ppc.S $(obj)/ghashp10-ppc.S: $(obj)/%.S: $(src)/%.pl FORCE
	$(call if_changed,perl)

$(obj)/aesp8-ppc.S $(obj)/ghashp8-ppc.S: $(obj)/%.S: $(src)/%.pl FORCE
	$(call if_changed,perl)

OBJECT_FILES_NON_STANDARD_aesp10-ppc.o := y
OBJECT_FILES_NON_STANDARD_ghashp10-ppc.o := y
OBJECT_FILES_NON_STANDARD_aesp8-ppc.o := y
