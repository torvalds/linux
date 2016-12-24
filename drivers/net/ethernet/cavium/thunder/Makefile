#
# Makefile for Cavium's Thunder ethernet device
#

obj-$(CONFIG_THUNDER_NIC_RGX) += thunder_xcv.o
obj-$(CONFIG_THUNDER_NIC_BGX) += thunder_bgx.o
obj-$(CONFIG_THUNDER_NIC_PF) += nicpf.o
obj-$(CONFIG_THUNDER_NIC_VF) += nicvf.o

nicpf-y := nic_main.o
nicvf-y := nicvf_main.o nicvf_queues.o
nicvf-y += nicvf_ethtool.o
