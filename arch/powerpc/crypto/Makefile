#
# powerpc/crypto/Makefile
#
# Arch-specific CryptoAPI modules.
#

obj-$(CONFIG_CRYPTO_SHA1_PPC) += sha1-powerpc.o
obj-$(CONFIG_CRYPTO_SHA256_PPC_SPE) += sha256-ppc-spe.o

sha1-powerpc-y := sha1-powerpc-asm.o sha1.o
sha256-ppc-spe-y := sha256-spe-asm.o sha256_spe_glue.o
