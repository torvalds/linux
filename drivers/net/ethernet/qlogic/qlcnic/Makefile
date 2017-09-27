#
# Makefile for Qlogic 1G/10G Ethernet Driver for CNA devices
#

obj-$(CONFIG_QLCNIC) := qlcnic.o

qlcnic-y := qlcnic_hw.o qlcnic_main.o qlcnic_init.o \
	qlcnic_ethtool.o qlcnic_ctx.o qlcnic_io.o \
	qlcnic_sysfs.o qlcnic_minidump.o qlcnic_83xx_hw.o \
	qlcnic_83xx_init.o qlcnic_83xx_vnic.o \
	qlcnic_sriov_common.o

qlcnic-$(CONFIG_QLCNIC_SRIOV) += qlcnic_sriov_pf.o

qlcnic-$(CONFIG_QLCNIC_DCB) += qlcnic_dcb.o
