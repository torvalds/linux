# SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
# Copyright (c) 2019-2020 Marvell International Ltd.

obj-$(CONFIG_QEDE) := qede.o

qede-y := qede_main.o qede_fp.o qede_filter.o qede_ethtool.o qede_ptp.o
qede-$(CONFIG_DCB) += qede_dcbnl.o
qede-$(CONFIG_QED_RDMA) += qede_rdma.o
