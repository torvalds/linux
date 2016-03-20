#
# Makefile for the HISILICON network device drivers.
#

obj-$(CONFIG_HNS) += hnae.o

obj-$(CONFIG_HNS_DSAF) += hns_dsaf.o
hns_dsaf-objs = hns_ae_adapt.o hns_dsaf_gmac.o hns_dsaf_mac.o hns_dsaf_misc.o \
	hns_dsaf_main.o hns_dsaf_ppe.o hns_dsaf_rcb.o hns_dsaf_xgmac.o

obj-$(CONFIG_HNS_ENET) += hns_enet_drv.o
hns_enet_drv-objs = hns_enet.o hns_ethtool.o
