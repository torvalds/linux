# SPDX-License-Identifier: GPL-2.0-only

obj-$(CONFIG_CRYPTO_BLAKE2S_ARM) += libblake2s-arm.o
libblake2s-arm-y := blake2s-core.o blake2s-glue.o

obj-$(CONFIG_CRYPTO_CHACHA20_NEON) += chacha-neon.o
chacha-neon-y := chacha-scalar-core.o chacha-glue.o
chacha-neon-$(CONFIG_KERNEL_MODE_NEON) += chacha-neon-core.o

obj-$(CONFIG_CRYPTO_POLY1305_ARM) += poly1305-arm.o
poly1305-arm-y := poly1305-core.o poly1305-glue.o

obj-$(CONFIG_CRYPTO_SHA256_ARM) += sha256-arm.o
sha256-arm-y := sha256.o sha256-core.o
sha256-arm-$(CONFIG_KERNEL_MODE_NEON) += sha256-ce.o

quiet_cmd_perl = PERL    $@
      cmd_perl = $(PERL) $(<) > $(@)

$(obj)/%-core.S: $(src)/%-armv4.pl
	$(call cmd,perl)

clean-files += poly1305-core.S sha256-core.S

aflags-thumb2-$(CONFIG_THUMB2_KERNEL)  := -U__thumb2__ -D__thumb2__=1

# massage the perlasm code a bit so we only get the NEON routine if we need it
poly1305-aflags-$(CONFIG_CPU_V7) := -U__LINUX_ARM_ARCH__ -D__LINUX_ARM_ARCH__=5
poly1305-aflags-$(CONFIG_KERNEL_MODE_NEON) := -U__LINUX_ARM_ARCH__ -D__LINUX_ARM_ARCH__=7
AFLAGS_poly1305-core.o += $(poly1305-aflags-y) $(aflags-thumb2-y)

AFLAGS_sha256-core.o += $(aflags-thumb2-y)
