#
# Arch-specific CryptoAPI modules.
#

obj-$(CONFIG_CRYPTO_AES_586) += aes-i586.o
obj-$(CONFIG_CRYPTO_TWOFISH_586) += twofish-i586.o
obj-$(CONFIG_CRYPTO_SALSA20_586) += salsa20-i586.o

obj-$(CONFIG_CRYPTO_AES_X86_64) += aes-x86_64.o
obj-$(CONFIG_CRYPTO_TWOFISH_X86_64) += twofish-x86_64.o
obj-$(CONFIG_CRYPTO_SALSA20_X86_64) += salsa20-x86_64.o
obj-$(CONFIG_CRYPTO_AES_NI_INTEL) += aesni-intel.o
obj-$(CONFIG_CRYPTO_GHASH_CLMUL_NI_INTEL) += ghash-clmulni-intel.o

obj-$(CONFIG_CRYPTO_CRC32C_INTEL) += crc32c-intel.o

aes-i586-y := aes-i586-asm_32.o aes_glue.o
twofish-i586-y := twofish-i586-asm_32.o twofish_glue.o
salsa20-i586-y := salsa20-i586-asm_32.o salsa20_glue.o

aes-x86_64-y := aes-x86_64-asm_64.o aes_glue.o
twofish-x86_64-y := twofish-x86_64-asm_64.o twofish_glue.o
salsa20-x86_64-y := salsa20-x86_64-asm_64.o salsa20_glue.o

aesni-intel-y := aesni-intel_asm.o aesni-intel_glue.o fpu.o

ghash-clmulni-intel-y := ghash-clmulni-intel_asm.o ghash-clmulni-intel_glue.o
