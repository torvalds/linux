# SPDX-License-Identifier: GPL-2.0

common-objs := enetc.o enetc_cbdr.o enetc_ethtool.o

obj-$(CONFIG_FSL_ENETC) += fsl-enetc.o
fsl-enetc-y := enetc_pf.o enetc_mdio.o $(common-objs)
fsl-enetc-$(CONFIG_PCI_IOV) += enetc_msg.o

obj-$(CONFIG_FSL_ENETC_VF) += fsl-enetc-vf.o
fsl-enetc-vf-y := enetc_vf.o $(common-objs)

obj-$(CONFIG_FSL_ENETC_MDIO) += fsl-enetc-mdio.o
fsl-enetc-mdio-y := enetc_pci_mdio.o enetc_mdio.o

obj-$(CONFIG_FSL_ENETC_PTP_CLOCK) += fsl-enetc-ptp.o
fsl-enetc-ptp-y := enetc_ptp.o
