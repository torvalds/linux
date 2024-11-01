# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_CRYPTO_DEV_NITROX_CNN55XX) += n5pf.o

n5pf-objs := nitrox_main.o \
	nitrox_isr.o \
	nitrox_lib.o \
	nitrox_hal.o \
	nitrox_reqmgr.o \
	nitrox_algs.o	\
	nitrox_mbx.o	\
	nitrox_skcipher.o \
	nitrox_aead.o

n5pf-$(CONFIG_PCI_IOV) += nitrox_sriov.o
n5pf-$(CONFIG_DEBUG_FS) += nitrox_debugfs.o
