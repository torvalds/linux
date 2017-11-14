#
# Makefile for the HISILICON network device drivers.
#

ccflags-y := -Idrivers/net/ethernet/hisilicon/hns3

obj-$(CONFIG_HNS3_HCLGE) += hclge.o
hclge-objs = hclge_main.o hclge_cmd.o hclge_mdio.o hclge_tm.o

obj-$(CONFIG_HNS3_ENET) += hns3.o
hns3-objs = hns3_enet.o hns3_ethtool.o
