# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2017 - 2019 Pensando Systems, Inc

obj-$(CONFIG_IONIC) := ionic.o

ionic-y := ionic_main.o ionic_bus_pci.o ionic_devlink.o ionic_dev.o \
	   ionic_debugfs.o ionic_lif.o ionic_rx_filter.o ionic_ethtool.o \
	   ionic_txrx.o ionic_stats.o ionic_fw.o
ionic-$(CONFIG_PTP_1588_CLOCK) += ionic_phc.o
