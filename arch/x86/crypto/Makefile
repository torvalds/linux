# SPDX-License-Identifier: GPL-2.0
#
# Arch-specific CryptoAPI modules.
#

OBJECT_FILES_NON_STANDARD := y

avx_supported := $(call as-instr,vpxor %xmm0$(comma)%xmm0$(comma)%xmm0,yes,no)
avx2_supported := $(call as-instr,vpgatherdd %ymm0$(comma)(%eax$(comma)%ymm1\
				$(comma)4)$(comma)%ymm2,yes,no)
avx512_supported :=$(call as-instr,vpmovm2b %k1$(comma)%zmm5,yes,no)
sha1_ni_supported :=$(call as-instr,sha1msg1 %xmm0$(comma)%xmm1,yes,no)
sha256_ni_supported :=$(call as-instr,sha256msg1 %xmm0$(comma)%xmm1,yes,no)

obj-$(CONFIG_CRYPTO_GLUE_HELPER_X86) += glue_helper.o

obj-$(CONFIG_CRYPTO_AES_586) += aes-i586.o
obj-$(CONFIG_CRYPTO_TWOFISH_586) += twofish-i586.o
obj-$(CONFIG_CRYPTO_SERPENT_SSE2_586) += serpent-sse2-i586.o

obj-$(CONFIG_CRYPTO_AES_X86_64) += aes-x86_64.o
obj-$(CONFIG_CRYPTO_DES3_EDE_X86_64) += des3_ede-x86_64.o
obj-$(CONFIG_CRYPTO_CAMELLIA_X86_64) += camellia-x86_64.o
obj-$(CONFIG_CRYPTO_BLOWFISH_X86_64) += blowfish-x86_64.o
obj-$(CONFIG_CRYPTO_TWOFISH_X86_64) += twofish-x86_64.o
obj-$(CONFIG_CRYPTO_TWOFISH_X86_64_3WAY) += twofish-x86_64-3way.o
obj-$(CONFIG_CRYPTO_CHACHA20_X86_64) += chacha-x86_64.o
obj-$(CONFIG_CRYPTO_SERPENT_SSE2_X86_64) += serpent-sse2-x86_64.o
obj-$(CONFIG_CRYPTO_AES_NI_INTEL) += aesni-intel.o
obj-$(CONFIG_CRYPTO_GHASH_CLMUL_NI_INTEL) += ghash-clmulni-intel.o

obj-$(CONFIG_CRYPTO_CRC32C_INTEL) += crc32c-intel.o
obj-$(CONFIG_CRYPTO_SHA1_SSSE3) += sha1-ssse3.o
obj-$(CONFIG_CRYPTO_CRC32_PCLMUL) += crc32-pclmul.o
obj-$(CONFIG_CRYPTO_SHA256_SSSE3) += sha256-ssse3.o
obj-$(CONFIG_CRYPTO_SHA512_SSSE3) += sha512-ssse3.o
obj-$(CONFIG_CRYPTO_CRCT10DIF_PCLMUL) += crct10dif-pclmul.o
obj-$(CONFIG_CRYPTO_POLY1305_X86_64) += poly1305-x86_64.o

obj-$(CONFIG_CRYPTO_AEGIS128_AESNI_SSE2) += aegis128-aesni.o
obj-$(CONFIG_CRYPTO_AEGIS128L_AESNI_SSE2) += aegis128l-aesni.o
obj-$(CONFIG_CRYPTO_AEGIS256_AESNI_SSE2) += aegis256-aesni.o

obj-$(CONFIG_CRYPTO_MORUS640_GLUE) += morus640_glue.o
obj-$(CONFIG_CRYPTO_MORUS1280_GLUE) += morus1280_glue.o

obj-$(CONFIG_CRYPTO_MORUS640_SSE2) += morus640-sse2.o
obj-$(CONFIG_CRYPTO_MORUS1280_SSE2) += morus1280-sse2.o

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
	obj-$(CONFIG_CRYPTO_CAMELLIA_AESNI_AVX2_X86_64) += camellia-aesni-avx2.o
	obj-$(CONFIG_CRYPTO_SERPENT_AVX2_X86_64) += serpent-avx2.o
	obj-$(CONFIG_CRYPTO_SHA1_MB) += sha1-mb/
	obj-$(CONFIG_CRYPTO_SHA256_MB) += sha256-mb/
	obj-$(CONFIG_CRYPTO_SHA512_MB) += sha512-mb/

	obj-$(CONFIG_CRYPTO_MORUS1280_AVX2) += morus1280-avx2.o
endif

aes-i586-y := aes-i586-asm_32.o aes_glue.o
twofish-i586-y := twofish-i586-asm_32.o twofish_glue.o
serpent-sse2-i586-y := serpent-sse2-i586-asm_32.o serpent_sse2_glue.o

aes-x86_64-y := aes-x86_64-asm_64.o aes_glue.o
des3_ede-x86_64-y := des3_ede-asm_64.o des3_ede_glue.o
camellia-x86_64-y := camellia-x86_64-asm_64.o camellia_glue.o
blowfish-x86_64-y := blowfish-x86_64-asm_64.o blowfish_glue.o
twofish-x86_64-y := twofish-x86_64-asm_64.o twofish_glue.o
twofish-x86_64-3way-y := twofish-x86_64-asm_64-3way.o twofish_glue_3way.o
chacha-x86_64-y := chacha-ssse3-x86_64.o chacha_glue.o
serpent-sse2-x86_64-y := serpent-sse2-x86_64-asm_64.o serpent_sse2_glue.o

aegis128-aesni-y := aegis128-aesni-asm.o aegis128-aesni-glue.o
aegis128l-aesni-y := aegis128l-aesni-asm.o aegis128l-aesni-glue.o
aegis256-aesni-y := aegis256-aesni-asm.o aegis256-aesni-glue.o

morus640-sse2-y := morus640-sse2-asm.o morus640-sse2-glue.o
morus1280-sse2-y := morus1280-sse2-asm.o morus1280-sse2-glue.o

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
	camellia-aesni-avx2-y := camellia-aesni-avx2-asm_64.o camellia_aesni_avx2_glue.o
	chacha-x86_64-y += chacha-avx2-x86_64.o
	serpent-avx2-y := serpent-avx2-asm_64.o serpent_avx2_glue.o

	morus1280-avx2-y := morus1280-avx2-asm.o morus1280-avx2-glue.o
endif

ifeq ($(avx512_supported),yes)
	chacha-x86_64-y += chacha-avx512vl-x86_64.o
endif

aesni-intel-y := aesni-intel_asm.o aesni-intel_glue.o fpu.o
aesni-intel-$(CONFIG_64BIT) += aesni-intel_avx-x86_64.o aes_ctrby8_avx-x86_64.o
ghash-clmulni-intel-y := ghash-clmulni-intel_asm.o ghash-clmulni-intel_glue.o
sha1-ssse3-y := sha1_ssse3_asm.o sha1_ssse3_glue.o
poly1305-x86_64-y := poly1305-sse2-x86_64.o poly1305_glue.o
ifeq ($(avx2_supported),yes)
sha1-ssse3-y += sha1_avx2_x86_64_asm.o
poly1305-x86_64-y += poly1305-avx2-x86_64.o
endif
ifeq ($(sha1_ni_supported),yes)
sha1-ssse3-y += sha1_ni_asm.o
endif
crc32c-intel-y := crc32c-intel_glue.o
crc32c-intel-$(CONFIG_64BIT) += crc32c-pcl-intel-asm_64.o
crc32-pclmul-y := crc32-pclmul_asm.o crc32-pclmul_glue.o
sha256-ssse3-y := sha256-ssse3-asm.o sha256-avx-asm.o sha256-avx2-asm.o sha256_ssse3_glue.o
ifeq ($(sha256_ni_supported),yes)
sha256-ssse3-y += sha256_ni_asm.o
endif
sha512-ssse3-y := sha512-ssse3-asm.o sha512-avx-asm.o sha512-avx2-asm.o sha512_ssse3_glue.o
crct10dif-pclmul-y := crct10dif-pcl-asm_64.o crct10dif-pclmul_glue.o
