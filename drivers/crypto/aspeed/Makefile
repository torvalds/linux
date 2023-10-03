hace-hash-$(CONFIG_CRYPTO_DEV_ASPEED_HACE_HASH) := aspeed-hace-hash.o
hace-crypto-$(CONFIG_CRYPTO_DEV_ASPEED_HACE_CRYPTO) := aspeed-hace-crypto.o

obj-$(CONFIG_CRYPTO_DEV_ASPEED) += aspeed_crypto.o
aspeed_crypto-objs := aspeed-hace.o	\
		      $(hace-hash-y)	\
		      $(hace-crypto-y)

aspeed_acry-$(CONFIG_CRYPTO_DEV_ASPEED_ACRY) += aspeed-acry.o
obj-$(CONFIG_CRYPTO_DEV_ASPEED) += $(aspeed_acry-y)

obj-$(CONFIG_CRYPTO_DEV_ASPEED_RSSS) += aspeed_rsss.o
aspeed_rsss-objs := aspeed-rsss.o aspeed-rsss-rsa.o aspeed-rsss-hash.o

aspeed_ecdsa-$(CONFIG_CRYPTO_DEV_ASPEED_ECDSA) += aspeed-ecdsa.o
obj-$(CONFIG_CRYPTO_DEV_ASPEED) += $(aspeed_ecdsa-y)
