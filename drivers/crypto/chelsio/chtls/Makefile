# SPDX-License-Identifier: GPL-2.0-only
ccflags-y := -I $(srctree)/drivers/net/ethernet/chelsio/cxgb4 \
	     -I $(srctree)/drivers/crypto/chelsio

obj-$(CONFIG_CRYPTO_DEV_CHELSIO_TLS) += chtls.o
chtls-objs := chtls_main.o chtls_cm.o chtls_io.o chtls_hw.o
