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

iavf-y := iavf_main.o iavf_ethtool.o iavf_virtchnl.o iavf_fdir.o \
	  iavf_adv_rss.o iavf_txrx.o iavf_common.o iavf_adminq.o

iavf-$(CONFIG_PTP_1588_CLOCK) += iavf_ptp.o
