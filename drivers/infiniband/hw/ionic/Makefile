# SPDX-License-Identifier: GPL-2.0

ccflags-y :=  -I $(srctree)/drivers/net/ethernet/pensando/ionic

obj-$(CONFIG_INFINIBAND_IONIC)	+= ionic_rdma.o

ionic_rdma-y :=	\
	ionic_ibdev.o ionic_lif_cfg.o ionic_queue.o ionic_pgtbl.o ionic_admin.o \
	ionic_controlpath.o ionic_datapath.o ionic_hw_stats.o
