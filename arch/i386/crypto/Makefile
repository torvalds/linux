# 
# i386/crypto/Makefile 
# 
# Arch-specific CryptoAPI modules.
# 

obj-$(CONFIG_CRYPTO_AES_586) += aes-i586.o
obj-$(CONFIG_CRYPTO_TWOFISH_586) += twofish-i586.o

aes-i586-y := aes-i586-asm_32.o aes.o
twofish-i586-y := twofish-i586-asm_32.o twofish_32.o

