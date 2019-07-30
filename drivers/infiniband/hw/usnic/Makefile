# SPDX-License-Identifier: GPL-2.0
ccflags-y := -I $(srctree)/drivers/net/ethernet/cisco/enic

obj-$(CONFIG_INFINIBAND_USNIC)+= usnic_verbs.o

usnic_verbs-y=\
usnic_fwd.o \
usnic_transport.o \
usnic_uiom.o \
usnic_uiom_interval_tree.o \
usnic_vnic.o \
usnic_ib_main.o \
usnic_ib_qp_grp.o \
usnic_ib_sysfs.o \
usnic_ib_verbs.o \
usnic_debugfs.o \
