#
# Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
# All rights reserved.
#

obj-$(CONFIG_BNA) += bna.o

bna-objs := bnad.o bnad_ethtool.o bnad_debugfs.o bna_enet.o bna_tx_rx.o
bna-objs += bfa_msgq.o bfa_ioc.o bfa_ioc_ct.o bfa_cee.o
bna-objs += cna_fwimg.o

EXTRA_CFLAGS := -Idrivers/net/bna
