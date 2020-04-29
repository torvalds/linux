# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_QEDE) := qede.o

qede-y := qede_main.o qede_fp.o qede_filter.o qede_ethtool.o qede_ptp.o
qede-$(CONFIG_DCB) += qede_dcbnl.o
qede-$(CONFIG_QED_RDMA) += qede_rdma.o
