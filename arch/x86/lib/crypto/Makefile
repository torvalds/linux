# SPDX-License-Identifier: GPL-2.0-only

obj-$(CONFIG_CRYPTO_BLAKE2S_X86) += libblake2s-x86_64.o
libblake2s-x86_64-y := blake2s-core.o blake2s-glue.o

obj-$(CONFIG_CRYPTO_CHACHA20_X86_64) += chacha-x86_64.o
chacha-x86_64-y := chacha-avx2-x86_64.o chacha-ssse3-x86_64.o chacha-avx512vl-x86_64.o chacha_glue.o

obj-$(CONFIG_CRYPTO_POLY1305_X86_64) += poly1305-x86_64.o
poly1305-x86_64-y := poly1305-x86_64-cryptogams.o poly1305_glue.o
targets += poly1305-x86_64-cryptogams.S

obj-$(CONFIG_CRYPTO_SHA256_X86_64) += sha256-x86_64.o
sha256-x86_64-y := sha256.o sha256-ssse3-asm.o sha256-avx-asm.o sha256-avx2-asm.o sha256-ni-asm.o

quiet_cmd_perlasm = PERLASM $@
      cmd_perlasm = $(PERL) $< > $@

$(obj)/%.S: $(src)/%.pl FORCE
	$(call if_changed,perlasm)
