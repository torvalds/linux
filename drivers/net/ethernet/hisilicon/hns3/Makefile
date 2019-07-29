# SPDX-License-Identifier: GPL-2.0+
#
# Makefile for the HISILICON network device drivers.
#

obj-$(CONFIG_HNS3) += hns3pf/
obj-$(CONFIG_HNS3) += hns3vf/

obj-$(CONFIG_HNS3) += hnae3.o

obj-$(CONFIG_HNS3_ENET) += hns3.o
hns3-objs = hns3_enet.o hns3_ethtool.o hns3_debugfs.o

hns3-$(CONFIG_HNS3_DCB) += hns3_dcbnl.o
