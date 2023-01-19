# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_FSL_ENETC_CORE) += fsl-enetc-core.o
fsl-enetc-core-y := enetc.o enetc_cbdr.o enetc_ethtool.o

obj-$(CONFIG_FSL_ENETC) += fsl-enetc.o
fsl-enetc-y := enetc_pf.o
fsl-enetc-$(CONFIG_PCI_IOV) += enetc_msg.o
fsl-enetc-$(CONFIG_FSL_ENETC_QOS) += enetc_qos.o

obj-$(CONFIG_FSL_ENETC_VF) += fsl-enetc-vf.o
fsl-enetc-vf-y := enetc_vf.o

obj-$(CONFIG_FSL_ENETC_IERB) += fsl-enetc-ierb.o
fsl-enetc-ierb-y := enetc_ierb.o

obj-$(CONFIG_FSL_ENETC_MDIO) += fsl-enetc-mdio.o
fsl-enetc-mdio-y := enetc_pci_mdio.o enetc_mdio.o

obj-$(CONFIG_FSL_ENETC_PTP_CLOCK) += fsl-enetc-ptp.o
fsl-enetc-ptp-y := enetc_ptp.o
