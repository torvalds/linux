# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Marvell's OcteonTX2 ethernet device drivers
#

obj-$(CONFIG_OCTEONTX2_PF) += octeontx2_nicpf.o
obj-$(CONFIG_OCTEONTX2_VF) += octeontx2_nicvf.o

octeontx2_nicpf-y := otx2_pf.o otx2_common.o otx2_txrx.o otx2_ethtool.o \
		     otx2_ptp.o
octeontx2_nicvf-y := otx2_vf.o

ccflags-y += -I$(srctree)/drivers/net/ethernet/marvell/octeontx2/af
