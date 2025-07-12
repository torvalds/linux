# SPDX-License-Identifier: GPL-2.0
#
# powerpc/crypto/Makefile
#
# Arch-specific CryptoAPI modules.
#

obj-$(CONFIG_CRYPTO_AES_PPC_SPE) += aes-ppc-spe.o
obj-$(CONFIG_CRYPTO_MD5_PPC) += md5-ppc.o
obj-$(CONFIG_CRYPTO_AES_GCM_P10) += aes-gcm-p10-crypto.o
obj-$(CONFIG_CRYPTO_DEV_VMX_ENCRYPT) += vmx-crypto.o
obj-$(CONFIG_CRYPTO_CURVE25519_PPC64) += curve25519-ppc64le.o

aes-ppc-spe-y := aes-spe-core.o aes-spe-keys.o aes-tab-4k.o aes-spe-modes.o aes-spe-glue.o
md5-ppc-y := md5-asm.o md5-glue.o
aes-gcm-p10-crypto-y := aes-gcm-p10-glue.o aes-gcm-p10.o ghashp10-ppc.o aesp10-ppc.o
vmx-crypto-objs := vmx.o aesp8-ppc.o ghashp8-ppc.o aes.o aes_cbc.o aes_ctr.o aes_xts.o ghash.o
curve25519-ppc64le-y := curve25519-ppc64le-core.o curve25519-ppc64le_asm.o

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
OBJECT_FILES_NON_STANDARD_ghashp8-ppc.o := y
