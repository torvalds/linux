# SPDX-License-Identifier: GPL-2.0
#
# Makefile for asymmetric cryptographic keys
#

krb5-y += \
	krb5_kdf.o \
	krb5_api.o \
	rfc3961_simplified.o \
	rfc3962_aes.o \
	rfc6803_camellia.o \
	rfc8009_aes2.o

krb5-$(CONFIG_CRYPTO_KRB5_SELFTESTS) += \
	selftest.o \
	selftest_data.o

obj-$(CONFIG_CRYPTO_KRB5) += krb5.o
