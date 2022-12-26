# SPDX-License-Identifier: GPL-2.0
sfc-y			+= efx.o efx_common.o efx_channels.o nic.o \
			   ef10.o \
			   tx.o tx_common.o tx_tso.o rx.o rx_common.o \
			   selftest.o ethtool.o ethtool_common.o ptp.o \
			   mcdi.o mcdi_port.o mcdi_port_common.o \
			   mcdi_functions.o mcdi_filters.o mcdi_mon.o \
			   ef100.o ef100_nic.o ef100_netdev.o \
			   ef100_ethtool.o ef100_rx.o ef100_tx.o
sfc-$(CONFIG_SFC_MTD)	+= mtd.o
sfc-$(CONFIG_SFC_SRIOV)	+= sriov.o ef10_sriov.o ef100_sriov.o ef100_rep.o \
                           mae.o tc.o tc_bindings.o tc_counters.o

obj-$(CONFIG_SFC)	+= sfc.o

obj-$(CONFIG_SFC_FALCON) += falcon/
obj-$(CONFIG_SFC_SIENA) += siena/
