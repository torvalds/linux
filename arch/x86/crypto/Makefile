#
# Arch-specific CryptoAPI modules.
#

obj-$(CONFIG_CRYPTO_AES_586) += aes-i586.o
obj-$(CONFIG_CRYPTO_TWOFISH_586) += twofish-i586.o

obj-$(CONFIG_CRYPTO_AES_X86_64) += aes-x86_64.o
obj-$(CONFIG_CRYPTO_TWOFISH_X86_64) += twofish-x86_64.o

aes-i586-y := aes-i586-asm_32.o aes_32.o
twofish-i586-y := twofish-i586-asm_32.o twofish_32.o

aes-x86_64-y := aes-x86_64-asm_64.o aes_64.o
twofish-x86_64-y := twofish-x86_64-asm_64.o twofish_64.o
