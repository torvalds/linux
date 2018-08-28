# SPDX-License-Identifier: GPL-2.0
sfc-y			+= efx.o nic.o farch.o siena.o ef10.o tx.o rx.o \
			   selftest.o ethtool.o ptp.o tx_tso.o \
			   mcdi.o mcdi_port.o mcdi_mon.o
sfc-$(CONFIG_SFC_MTD)	+= mtd.o
sfc-$(CONFIG_SFC_SRIOV)	+= sriov.o siena_sriov.o ef10_sriov.o

obj-$(CONFIG_SFC)	+= sfc.o

obj-$(CONFIG_SFC_FALCON) += falcon/
