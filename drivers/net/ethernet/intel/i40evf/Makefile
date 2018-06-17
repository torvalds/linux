# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2013 - 2018 Intel Corporation.

#
## Makefile for the Intel(R) 40GbE VF driver
#
#

ccflags-y += -I$(src)
subdir-ccflags-y += -I$(src)

obj-$(CONFIG_I40EVF) += i40evf.o

i40evf-objs :=	i40evf_main.o i40evf_ethtool.o i40evf_virtchnl.o \
		i40e_txrx.o i40e_common.o i40e_adminq.o i40evf_client.o

