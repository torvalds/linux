#
# Makefile for asymmetric cryptographic keys
#

obj-$(CONFIG_ASYMMETRIC_KEY_TYPE) += asymmetric_keys.o

asymmetric_keys-y := asymmetric_type.o signature.o

obj-$(CONFIG_ASYMMETRIC_PUBLIC_KEY_SUBTYPE) += public_key.o
obj-$(CONFIG_PUBLIC_KEY_ALGO_RSA) += rsa.o
