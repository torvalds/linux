# SPDX-License-Identifier: GPL-2.0
#
# x86 crypto algorithms

OBJECT_FILES_NON_STANDARD := y

obj-$(CONFIG_CRYPTO_GLUE_HELPER_X86) += glue_helper.o

obj-$(CONFIG_CRYPTO_TWOFISH_586) += twofish-i586.o
twofish-i586-y := twofish-i586-asm_32.o twofish_glue.o
obj-$(CONFIG_CRYPTO_TWOFISH_X86_64) += twofish-x86_64.o
twofish-x86_64-y := twofish-x86_64-asm_64.o twofish_glue.o
obj-$(CONFIG_CRYPTO_TWOFISH_X86_64_3WAY) += twofish-x86_64-3way.o
twofish-x86_64-3way-y := twofish-x86_64-asm_64-3way.o twofish_glue_3way.o
obj-$(CONFIG_CRYPTO_TWOFISH_AVX_X86_64) += twofish-avx-x86_64.o
twofish-avx-x86_64-y := twofish-avx-x86_64-asm_64.o twofish_avx_glue.o

obj-$(CONFIG_CRYPTO_SERPENT_SSE2_586) += serpent-sse2-i586.o
serpent-sse2-i586-y := serpent-sse2-i586-asm_32.o serpent_sse2_glue.o
obj-$(CONFIG_CRYPTO_SERPENT_SSE2_X86_64) += serpent-sse2-x86_64.o
serpent-sse2-x86_64-y := serpent-sse2-x86_64-asm_64.o serpent_sse2_glue.o
obj-$(CONFIG_CRYPTO_SERPENT_AVX_X86_64) += serpent-avx-x86_64.o
serpent-avx-x86_64-y := serpent-avx-x86_64-asm_64.o serpent_avx_glue.o
obj-$(CONFIG_CRYPTO_SERPENT_AVX2_X86_64) += serpent-avx2.o
serpent-avx2-y := serpent-avx2-asm_64.o serpent_avx2_glue.o

obj-$(CONFIG_CRYPTO_DES3_EDE_X86_64) += des3_ede-x86_64.o
des3_ede-x86_64-y := des3_ede-asm_64.o des3_ede_glue.o

obj-$(CONFIG_CRYPTO_CAMELLIA_X86_64) += camellia-x86_64.o
camellia-x86_64-y := camellia-x86_64-asm_64.o camellia_glue.o
obj-$(CONFIG_CRYPTO_CAMELLIA_AESNI_AVX_X86_64) += camellia-aesni-avx-x86_64.o
camellia-aesni-avx-x86_64-y := camellia-aesni-avx-asm_64.o camellia_aesni_avx_glue.o
obj-$(CONFIG_CRYPTO_CAMELLIA_AESNI_AVX2_X86_64) += camellia-aesni-avx2.o
camellia-aesni-avx2-y := camellia-aesni-avx2-asm_64.o camellia_aesni_avx2_glue.o

obj-$(CONFIG_CRYPTO_BLOWFISH_X86_64) += blowfish-x86_64.o
blowfish-x86_64-y := blowfish-x86_64-asm_64.o blowfish_glue.o

obj-$(CONFIG_CRYPTO_CAST5_AVX_X86_64) += cast5-avx-x86_64.o
cast5-avx-x86_64-y := cast5-avx-x86_64-asm_64.o cast5_avx_glue.o

obj-$(CONFIG_CRYPTO_CAST6_AVX_X86_64) += cast6-avx-x86_64.o
cast6-avx-x86_64-y := cast6-avx-x86_64-asm_64.o cast6_avx_glue.o

obj-$(CONFIG_CRYPTO_AEGIS128_AESNI_SSE2) += aegis128-aesni.o
aegis128-aesni-y := aegis128-aesni-asm.o aegis128-aesni-glue.o

obj-$(CONFIG_CRYPTO_CHACHA20_X86_64) += chacha-x86_64.o
chacha-x86_64-y := chacha-avx2-x86_64.o chacha-ssse3-x86_64.o chacha_glue.o
chacha-x86_64-$(CONFIG_AS_AVX512) += chacha-avx512vl-x86_64.o

obj-$(CONFIG_CRYPTO_AES_NI_INTEL) += aesni-intel.o
aesni-intel-y := aesni-intel_asm.o aesni-intel_glue.o
aesni-intel-$(CONFIG_64BIT) += aesni-intel_avx-x86_64.o aes_ctrby8_avx-x86_64.o

obj-$(CONFIG_CRYPTO_SHA1_SSSE3) += sha1-ssse3.o
sha1-ssse3-y := sha1_avx2_x86_64_asm.o sha1_ssse3_asm.o sha1_ssse3_glue.o
sha1-ssse3-$(CONFIG_AS_SHA1_NI) += sha1_ni_asm.o

obj-$(CONFIG_CRYPTO_SHA256_SSSE3) += sha256-ssse3.o
sha256-ssse3-y := sha256-ssse3-asm.o sha256-avx-asm.o sha256-avx2-asm.o sha256_ssse3_glue.o
sha256-ssse3-$(CONFIG_AS_SHA256_NI) += sha256_ni_asm.o

obj-$(CONFIG_CRYPTO_SHA512_SSSE3) += sha512-ssse3.o
sha512-ssse3-y := sha512-ssse3-asm.o sha512-avx-asm.o sha512-avx2-asm.o sha512_ssse3_glue.o

obj-$(CONFIG_CRYPTO_BLAKE2S_X86) += blake2s-x86_64.o
blake2s-x86_64-y := blake2s-core.o blake2s-glue.o

obj-$(CONFIG_CRYPTO_GHASH_CLMUL_NI_INTEL) += ghash-clmulni-intel.o
ghash-clmulni-intel-y := ghash-clmulni-intel_asm.o ghash-clmulni-intel_glue.o

obj-$(CONFIG_CRYPTO_CRC32C_INTEL) += crc32c-intel.o
crc32c-intel-y := crc32c-intel_glue.o
crc32c-intel-$(CONFIG_64BIT) += crc32c-pcl-intel-asm_64.o

obj-$(CONFIG_CRYPTO_CRC32_PCLMUL) += crc32-pclmul.o
crc32-pclmul-y := crc32-pclmul_asm.o crc32-pclmul_glue.o

obj-$(CONFIG_CRYPTO_CRCT10DIF_PCLMUL) += crct10dif-pclmul.o
crct10dif-pclmul-y := crct10dif-pcl-asm_64.o crct10dif-pclmul_glue.o

obj-$(CONFIG_CRYPTO_POLY1305_X86_64) += poly1305-x86_64.o
poly1305-x86_64-y := poly1305-x86_64-cryptogams.o poly1305_glue.o
targets += poly1305-x86_64-cryptogams.S

obj-$(CONFIG_CRYPTO_NHPOLY1305_SSE2) += nhpoly1305-sse2.o
nhpoly1305-sse2-y := nh-sse2-x86_64.o nhpoly1305-sse2-glue.o
obj-$(CONFIG_CRYPTO_NHPOLY1305_AVX2) += nhpoly1305-avx2.o
nhpoly1305-avx2-y := nh-avx2-x86_64.o nhpoly1305-avx2-glue.o

obj-$(CONFIG_CRYPTO_CURVE25519_X86) += curve25519-x86_64.o

quiet_cmd_perlasm = PERLASM $@
      cmd_perlasm = $(PERL) $< > $@
$(obj)/%.S: $(src)/%.pl FORCE
	$(call if_changed,perlasm)
