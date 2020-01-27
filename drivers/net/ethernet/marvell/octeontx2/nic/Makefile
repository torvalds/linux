# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Marvell's OcteonTX2 ethernet device drivers
#

obj-$(CONFIG_OCTEONTX2_PF) += octeontx2_nicpf.o

octeontx2_nicpf-y := otx2_pf.o otx2_common.o otx2_txrx.o otx2_ethtool.o

ccflags-y += -I$(srctree)/drivers/net/ethernet/marvell/octeontx2/af
