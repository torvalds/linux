# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Ethernet Bonding driver
#

obj-$(CONFIG_BONDING) += bonding.o

bonding-objs := bond_main.o bond_3ad.o bond_alb.o bond_sysfs.o bond_sysfs_slave.o bond_debugfs.o bond_netlink.o bond_options.o

proc-$(CONFIG_PROC_FS) += bond_procfs.o
bonding-objs += $(proc-y)

