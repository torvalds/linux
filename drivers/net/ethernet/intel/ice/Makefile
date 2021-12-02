# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2018, Intel Corporation.

#
# Makefile for the Intel(R) Ethernet Connection E800 Series Linux Driver
#

obj-$(CONFIG_ICE) += ice.o

ice-y := ice_main.o	\
	 ice_controlq.o	\
	 ice_common.o	\
	 ice_nvm.o	\
	 ice_switch.o	\
	 ice_sched.o	\
	 ice_base.o	\
	 ice_lib.o	\
	 ice_txrx_lib.o	\
	 ice_txrx.o	\
	 ice_fltr.o	\
	 ice_vsi_vlan_ops.o \
	 ice_vsi_vlan_lib.o \
	 ice_fdir.o	\
	 ice_ethtool_fdir.o \
	 ice_flex_pipe.o \
	 ice_flow.o	\
	 ice_idc.o	\
	 ice_devlink.o	\
	 ice_fw_update.o \
	 ice_lag.o	\
	 ice_ethtool.o  \
	 ice_repr.o	\
	 ice_tc_lib.o
ice-$(CONFIG_PCI_IOV) += ice_virtchnl_allowlist.o
ice-$(CONFIG_PCI_IOV) += ice_virtchnl_pf.o ice_sriov.o ice_virtchnl_fdir.o
ice-$(CONFIG_PTP_1588_CLOCK) += ice_ptp.o ice_ptp_hw.o
ice-$(CONFIG_DCB) += ice_dcb.o ice_dcb_nl.o ice_dcb_lib.o
ice-$(CONFIG_RFS_ACCEL) += ice_arfs.o
ice-$(CONFIG_XDP_SOCKETS) += ice_xsk.o
ice-$(CONFIG_ICE_SWITCHDEV) += ice_eswitch.o
