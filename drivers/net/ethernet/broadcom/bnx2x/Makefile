# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for Broadcom 10-Gigabit ethernet driver
#

obj-$(CONFIG_BNX2X) += bnx2x.o

bnx2x-y := bnx2x_main.o bnx2x_link.o bnx2x_cmn.o bnx2x_ethtool.o bnx2x_stats.o bnx2x_dcb.o bnx2x_sp.o
bnx2x-$(CONFIG_BNX2X_SRIOV) += bnx2x_vfpf.o bnx2x_sriov.o
