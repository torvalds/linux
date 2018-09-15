# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2013 - 2018 Intel Corporation.
#
# Makefile for the Intel(R) Ethernet Adaptive Virtual Function (iavf)
# driver
#
#

ccflags-y += -I$(src)
subdir-ccflags-y += -I$(src)

obj-$(CONFIG_IAVF) += iavf.o

iavf-objs := i40evf_main.o i40evf_ethtool.o i40evf_virtchnl.o \
	      i40e_txrx.o i40e_common.o i40e_adminq.o i40evf_client.o
