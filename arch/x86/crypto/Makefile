#
# Arch-specific CryptoAPI modules.
#

obj-$(CONFIG_CRYPTO_AES_586) += aes-i586.o
obj-$(CONFIG_CRYPTO_TWOFISH_586) += twofish-i586.o
obj-$(CONFIG_CRYPTO_SALSA20_586) += salsa20-i586.o
obj-$(CONFIG_CRYPTO_SERPENT_SSE2_586) += serpent-sse2-i586.o

obj-$(CONFIG_CRYPTO_AES_X86_64) += aes-x86_64.o
obj-$(CONFIG_CRYPTO_CAMELLIA_X86_64) += camellia-x86_64.o
obj-$(CONFIG_CRYPTO_BLOWFISH_X86_64) += blowfish-x86_64.o
obj-$(CONFIG_CRYPTO_TWOFISH_X86_64) += twofish-x86_64.o
obj-$(CONFIG_CRYPTO_TWOFISH_X86_64_3WAY) += twofish-x86_64-3way.o
obj-$(CONFIG_CRYPTO_SALSA20_X86_64) += salsa20-x86_64.o
obj-$(CONFIG_CRYPTO_SERPENT_SSE2_X86_64) += serpent-sse2-x86_64.o
obj-$(CONFIG_CRYPTO_AES_NI_INTEL) += aesni-intel.o
obj-$(CONFIG_CRYPTO_GHASH_CLMUL_NI_INTEL) += ghash-clmulni-intel.o

obj-$(CONFIG_CRYPTO_CRC32C_INTEL) += crc32c-intel.o
obj-$(CONFIG_CRYPTO_SHA1_SSSE3) += sha1-ssse3.o

aes-i586-y := aes-i586-asm_32.o aes_glue.o
twofish-i586-y := twofish-i586-asm_32.o twofish_glue.o
salsa20-i586-y := salsa20-i586-asm_32.o salsa20_glue.o
serpent-sse2-i586-y := serpent-sse2-i586-asm_32.o serpent_sse2_glue.o

aes-x86_64-y := aes-x86_64-asm_64.o aes_glue.o
camellia-x86_64-y := camellia-x86_64-asm_64.o camellia_glue.o
blowfish-x86_64-y := blowfish-x86_64-asm_64.o blowfish_glue.o
twofish-x86_64-y := twofish-x86_64-asm_64.o twofish_glue.o
twofish-x86_64-3way-y := twofish-x86_64-asm_64-3way.o twofish_glue_3way.o
salsa20-x86_64-y := salsa20-x86_64-asm_64.o salsa20_glue.o
serpent-sse2-x86_64-y := serpent-sse2-x86_64-asm_64.o serpent_sse2_glue.o

aesni-intel-y := aesni-intel_asm.o aesni-intel_glue.o fpu.o

ghash-clmulni-intel-y := ghash-clmulni-intel_asm.o ghash-clmulni-intel_glue.o

# enable AVX support only when $(AS) can actually assemble the instructions
ifeq ($(call as-instr,vpxor %xmm0$(comma)%xmm1$(comma)%xmm2,yes,no),yes)
AFLAGS_sha1_ssse3_asm.o += -DSHA1_ENABLE_AVX_SUPPORT
CFLAGS_sha1_ssse3_glue.o += -DSHA1_ENABLE_AVX_SUPPORT
endif
sha1-ssse3-y := sha1_ssse3_asm.o sha1_ssse3_glue.o
