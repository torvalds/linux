# SPDX-License-Identifier: GPL-2.0
sfc-siena-y		+= farch.o siena.o \
			   efx.o efx_common.o efx_channels.o nic.o \
			   tx.o tx_common.o rx.o rx_common.o \
			   selftest.o ethtool.o ethtool_common.o ptp.o \
			   mcdi.o mcdi_port.o mcdi_port_common.o \
			   mcdi_mon.o
sfc-siena-$(CONFIG_SFC_SIENA_MTD)	+= mtd.o
sfc-siena-$(CONFIG_SFC_SRIOV)		+= siena_sriov.o

obj-$(CONFIG_SFC_SIENA)	+= sfc-siena.o
