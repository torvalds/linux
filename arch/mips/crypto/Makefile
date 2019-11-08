# SPDX-License-Identifier: GPL-2.0
#
# Makefile for MIPS crypto files..
#

obj-$(CONFIG_CRYPTO_CRC32_MIPS) += crc32-mips.o

obj-$(CONFIG_CRYPTO_CHACHA_MIPS) += chacha-mips.o
chacha-mips-y := chacha-core.o chacha-glue.o
AFLAGS_chacha-core.o += -O2 # needed to fill branch delay slots

obj-$(CONFIG_CRYPTO_POLY1305_MIPS) += poly1305-mips.o
poly1305-mips-y := poly1305-core.o poly1305-glue.o

perlasm-flavour-$(CONFIG_CPU_MIPS32) := o32
perlasm-flavour-$(CONFIG_CPU_MIPS64) := 64

quiet_cmd_perlasm = PERLASM $@
      cmd_perlasm = $(PERL) $(<) $(perlasm-flavour-y) $(@)

$(obj)/poly1305-core.S: $(src)/poly1305-mips.pl FORCE
	$(call if_changed,perlasm)

targets += poly1305-core.S
