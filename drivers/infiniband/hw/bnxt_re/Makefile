# SPDX-License-Identifier: GPL-2.0

ccflags-y := -Idrivers/net/ethernet/broadcom/bnxt
obj-$(CONFIG_INFINIBAND_BNXT_RE) += bnxt_re.o
bnxt_re-y := main.o ib_verbs.o \
	     qplib_res.o qplib_rcfw.o	\
	     qplib_sp.o qplib_fp.o  hw_counters.o
