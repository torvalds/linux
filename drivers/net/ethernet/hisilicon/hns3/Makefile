# SPDX-License-Identifier: GPL-2.0+
#
# Makefile for the HISILICON network device drivers.
#

ccflags-y += -I$(srctree)/$(src)
ccflags-y += -I$(srctree)/drivers/net/ethernet/hisilicon/hns3/hns3pf
ccflags-y += -I$(srctree)/drivers/net/ethernet/hisilicon/hns3/hns3vf
ccflags-y += -I$(srctree)/drivers/net/ethernet/hisilicon/hns3/hns3_common

obj-$(CONFIG_HNS3) += hnae3.o

obj-$(CONFIG_HNS3_ENET) += hns3.o
hns3-objs = hns3_enet.o hns3_ethtool.o hns3_debugfs.o

hns3-$(CONFIG_HNS3_DCB) += hns3_dcbnl.o

obj-$(CONFIG_HNS3_HCLGEVF) += hclgevf.o

hclgevf-objs = hns3vf/hclgevf_main.o hns3vf/hclgevf_mbx.o  hns3vf/hclgevf_devlink.o hns3vf/hclgevf_regs.o \
		hns3_common/hclge_comm_cmd.o hns3_common/hclge_comm_rss.o hns3_common/hclge_comm_tqp_stats.o

obj-$(CONFIG_HNS3_HCLGE) += hclge.o
hclge-objs = hns3pf/hclge_main.o hns3pf/hclge_mdio.o hns3pf/hclge_tm.o hns3pf/hclge_regs.o \
		hns3pf/hclge_mbx.o hns3pf/hclge_err.o  hns3pf/hclge_debugfs.o hns3pf/hclge_ptp.o hns3pf/hclge_devlink.o \
		hns3_common/hclge_comm_cmd.o hns3_common/hclge_comm_rss.o hns3_common/hclge_comm_tqp_stats.o


hclge-$(CONFIG_HNS3_DCB) += hns3pf/hclge_dcb.o
