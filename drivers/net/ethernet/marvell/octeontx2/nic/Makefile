# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Marvell's RVU Ethernet device drivers
#

obj-$(CONFIG_OCTEONTX2_PF) += rvu_nicpf.o otx2_ptp.o
obj-$(CONFIG_OCTEONTX2_VF) += rvu_nicvf.o otx2_ptp.o

rvu_nicpf-y := otx2_pf.o otx2_common.o otx2_txrx.o otx2_ethtool.o \
               otx2_flows.o otx2_tc.o cn10k.o otx2_dmac_flt.o \
               otx2_devlink.o qos_sq.o qos.o
rvu_nicvf-y := otx2_vf.o otx2_devlink.o

rvu_nicpf-$(CONFIG_DCB) += otx2_dcbnl.o
rvu_nicvf-$(CONFIG_DCB) += otx2_dcbnl.o
rvu_nicpf-$(CONFIG_MACSEC) += cn10k_macsec.o

ccflags-y += -I$(srctree)/drivers/net/ethernet/marvell/octeontx2/af
