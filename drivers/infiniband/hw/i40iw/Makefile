# SPDX-License-Identifier: GPL-2.0
ccflags-y :=  -I $(srctree)/drivers/net/ethernet/intel/i40e

obj-$(CONFIG_INFINIBAND_I40IW) += i40iw.o

i40iw-objs :=\
               i40iw_cm.o i40iw_ctrl.o \
               i40iw_hmc.o i40iw_hw.o i40iw_main.o  \
               i40iw_pble.o i40iw_puda.o i40iw_uk.o i40iw_utils.o \
               i40iw_verbs.o i40iw_virtchnl.o i40iw_vf.o
