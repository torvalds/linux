# SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
# Copyright (c) 2019, Intel Corporation.

#
# Makefile for the Intel(R) Ethernet Connection RDMA Linux Driver
#

obj-$(CONFIG_INFINIBAND_IRDMA) += irdma.o

irdma-objs := cm.o        \
              ctrl.o      \
              hmc.o       \
              hw.o        \
              i40iw_hw.o  \
              i40iw_if.o  \
	      ig3rdma_if.o\
	      icrdma_if.o \
              icrdma_hw.o \
	      ig3rdma_hw.o\
              main.o      \
              pble.o      \
              puda.o      \
              trace.o     \
              uda.o       \
              uk.o        \
              utils.o     \
              verbs.o     \
	      virtchnl.o  \
              ws.o        \

CFLAGS_trace.o = -I$(src)
