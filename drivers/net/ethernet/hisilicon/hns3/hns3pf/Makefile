# SPDX-License-Identifier: GPL-2.0+
#
# Makefile for the HISILICON network device drivers.
#

ccflags-y := -Idrivers/net/ethernet/hisilicon/hns3

obj-$(CONFIG_HNS3_HCLGE) += hclge.o
hclge-objs = hclge_main.o hclge_cmd.o hclge_mdio.o hclge_tm.o hclge_mbx.o hclge_err.o  hclge_debugfs.o

hclge-$(CONFIG_HNS3_DCB) += hclge_dcb.o
