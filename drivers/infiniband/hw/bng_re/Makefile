# SPDX-License-Identifier: GPL-2.0
ccflags-y := -I $(srctree)/drivers/net/ethernet/broadcom/bnge -I $(srctree)/drivers/infiniband/hw/bnxt_re

obj-$(CONFIG_INFINIBAND_BNG_RE) += bng_re.o

bng_re-y := bng_dev.o bng_fw.o \
	    bng_res.o bng_sp.o \
	    bng_debugfs.o
