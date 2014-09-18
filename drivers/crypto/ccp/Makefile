obj-$(CONFIG_CRYPTO_DEV_CCP_DD) += ccp.o
ccp-objs := ccp-dev.o ccp-ops.o
ifdef CONFIG_X86
ccp-objs += ccp-pci.o
endif
ifdef CONFIG_ARM64
ccp-objs += ccp-platform.o
endif

obj-$(CONFIG_CRYPTO_DEV_CCP_CRYPTO) += ccp-crypto.o
ccp-crypto-objs := ccp-crypto-main.o \
		   ccp-crypto-aes.o \
		   ccp-crypto-aes-cmac.o \
		   ccp-crypto-aes-xts.o \
		   ccp-crypto-sha.o
