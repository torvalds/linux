# SPDX-License-Identifier: GPL-2.0-only
################################################################################
#
# aQuantia Ethernet Controller AQtion Linux Driver
# Copyright(c) 2014-2017 aQuantia Corporation.
#
################################################################################

obj-$(CONFIG_AQTION) += atlantic.o

ccflags-y += -I$(src)

atlantic-objs := aq_main.o \
	aq_nic.o \
	aq_pci_func.o \
	aq_vec.o \
	aq_ring.o \
	aq_hw_utils.o \
	aq_ethtool.o \
	aq_drvinfo.o \
	aq_filters.o \
	aq_phy.o \
	hw_atl/hw_atl_a0.o \
	hw_atl/hw_atl_b0.o \
	hw_atl/hw_atl_utils.o \
	hw_atl/hw_atl_utils_fw2x.o \
	hw_atl/hw_atl_llh.o \
	hw_atl2/hw_atl2.o \
	hw_atl2/hw_atl2_utils.o \
	hw_atl2/hw_atl2_utils_fw.o \
	hw_atl2/hw_atl2_llh.o \
	macsec/macsec_api.o

atlantic-$(CONFIG_MACSEC) += aq_macsec.o

atlantic-$(CONFIG_PTP_1588_CLOCK) += aq_ptp.o
