obj-$(CONFIG_CRYPTO_DEV_CCP_DD) += ccp.o
ccp-objs := ccp-dev.o ccp-ops.o ccp-dev-v3.o ccp-platform.o
ccp-$(CONFIG_PCI) += ccp-pci.o

obj-$(CONFIG_CRYPTO_DEV_CCP_CRYPTO) += ccp-crypto.o
ccp-crypto-objs := ccp-crypto-main.o \
		   ccp-crypto-aes.o \
		   ccp-crypto-aes-cmac.o \
		   ccp-crypto-aes-xts.o \
		   ccp-crypto-sha.o
