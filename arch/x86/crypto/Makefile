#
# Arch-specific CryptoAPI modules.
#

avx_supported := $(call as-instr,vpxor %xmm0$(comma)%xmm0$(comma)%xmm0,yes,no)
avx2_supported := $(call as-instr,vpgatherdd %ymm0$(comma)(%eax$(comma)%ymm1\
					$(comma)4)$(comma)%ymm2,yes,no)

obj-$(CONFIG_CRYPTO_ABLK_HELPER_X86) += ablk_helper.o
obj-$(CONFIG_CRYPTO_GLUE_HELPER_X86) += glue_helper.o

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
obj-$(CONFIG_CRYPTO_CRC32_PCLMUL) += crc32-pclmul.o
obj-$(CONFIG_CRYPTO_SHA256_SSSE3) += sha256-ssse3.o
obj-$(CONFIG_CRYPTO_SHA512_SSSE3) += sha512-ssse3.o
obj-$(CONFIG_CRYPTO_CRCT10DIF_PCLMUL) += crct10dif-pclmul.o

# These modules require assembler to support AVX.
ifeq ($(avx_supported),yes)
	obj-$(CONFIG_CRYPTO_CAMELLIA_AESNI_AVX_X86_64) += \
						camellia-aesni-avx-x86_64.o
	obj-$(CONFIG_CRYPTO_CAST5_AVX_X86_64) += cast5-avx-x86_64.o
	obj-$(CONFIG_CRYPTO_CAST6_AVX_X86_64) += cast6-avx-x86_64.o
	obj-$(CONFIG_CRYPTO_TWOFISH_AVX_X86_64) += twofish-avx-x86_64.o
	obj-$(CONFIG_CRYPTO_SERPENT_AVX_X86_64) += serpent-avx-x86_64.o
endif

# These modules require assembler to support AVX2.
ifeq ($(avx2_supported),yes)
	obj-$(CONFIG_CRYPTO_BLOWFISH_AVX2_X86_64) += blowfish-avx2.o
	obj-$(CONFIG_CRYPTO_CAMELLIA_AESNI_AVX2_X86_64) += camellia-aesni-avx2.o
	obj-$(CONFIG_CRYPTO_SERPENT_AVX2_X86_64) += serpent-avx2.o
	obj-$(CONFIG_CRYPTO_TWOFISH_AVX2_X86_64) += twofish-avx2.o
endif

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

ifeq ($(avx_supported),yes)
	camellia-aesni-avx-x86_64-y := camellia-aesni-avx-asm_64.o \
					camellia_aesni_avx_glue.o
	cast5-avx-x86_64-y := cast5-avx-x86_64-asm_64.o cast5_avx_glue.o
	cast6-avx-x86_64-y := cast6-avx-x86_64-asm_64.o cast6_avx_glue.o
	twofish-avx-x86_64-y := twofish-avx-x86_64-asm_64.o \
				twofish_avx_glue.o
	serpent-avx-x86_64-y := serpent-avx-x86_64-asm_64.o \
				serpent_avx_glue.o
endif

ifeq ($(avx2_supported),yes)
	blowfish-avx2-y := blowfish-avx2-asm_64.o blowfish_avx2_glue.o
	camellia-aesni-avx2-y := camellia-aesni-avx2-asm_64.o camellia_aesni_avx2_glue.o
	serpent-avx2-y := serpent-avx2-asm_64.o serpent_avx2_glue.o
	twofish-avx2-y := twofish-avx2-asm_64.o twofish_avx2_glue.o
endif

aesni-intel-y := aesni-intel_asm.o aesni-intel_glue.o fpu.o
ghash-clmulni-intel-y := ghash-clmulni-intel_asm.o ghash-clmulni-intel_glue.o
sha1-ssse3-y := sha1_ssse3_asm.o sha1_ssse3_glue.o
crc32c-intel-y := crc32c-intel_glue.o
crc32c-intel-$(CONFIG_64BIT) += crc32c-pcl-intel-asm_64.o
crc32-pclmul-y := crc32-pclmul_asm.o crc32-pclmul_glue.o
sha256-ssse3-y := sha256-ssse3-asm.o sha256-avx-asm.o sha256-avx2-asm.o sha256_ssse3_glue.o
sha512-ssse3-y := sha512-ssse3-asm.o sha512-avx-asm.o sha512-avx2-asm.o sha512_ssse3_glue.o
crct10dif-pclmul-y := crct10dif-pcl-asm_64.o crct10dif-pclmul_glue.o
