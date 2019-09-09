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
	 ice_lib.o	\
	 ice_txrx.o	\
	 ice_ethtool.o
ice-$(CONFIG_PCI_IOV) += ice_virtchnl_pf.o ice_sriov.o
ice-$(CONFIG_DCB) += ice_dcb.o ice_dcb_lib.o
