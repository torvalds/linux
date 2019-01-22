# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_FSL_ENETC) += fsl-enetc.o
fsl-enetc-$(CONFIG_FSL_ENETC) += enetc.o enetc_cbdr.o enetc_ethtool.o
fsl-enetc-objs := enetc_pf.o $(fsl-enetc-y)

obj-$(CONFIG_FSL_ENETC_VF) += fsl-enetc-vf.o

ifeq ($(CONFIG_FSL_ENETC)$(CONFIG_FSL_ENETC_VF), yy)
fsl-enetc-vf-objs := enetc_vf.o
else
fsl-enetc-vf-$(CONFIG_FSL_ENETC_VF) += enetc.o enetc_cbdr.o \
				       enetc_ethtool.o
fsl-enetc-vf-objs := enetc_vf.o $(fsl-enetc-vf-y)
endif
