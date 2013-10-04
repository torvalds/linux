#
# Makefile for the CAAM backend and dependent components
#
ifeq ($(CONFIG_CRYPTO_DEV_FSL_CAAM_DEBUG), y)
	EXTRA_CFLAGS := -DDEBUG
endif

obj-$(CONFIG_CRYPTO_DEV_FSL_CAAM) += caam.o
obj-$(CONFIG_CRYPTO_DEV_FSL_CAAM_CRYPTO_API) += caamalg.o
obj-$(CONFIG_CRYPTO_DEV_FSL_CAAM_AHASH_API) += caamhash.o
obj-$(CONFIG_CRYPTO_DEV_FSL_CAAM_RNG_API) += caamrng.o

caam-objs := ctrl.o jr.o error.o key_gen.o
