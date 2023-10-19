# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_BNXT) += bnxt_en.o

bnxt_en-y := bnxt.o bnxt_hwrm.o bnxt_sriov.o bnxt_ethtool.o bnxt_dcb.o bnxt_ulp.o bnxt_xdp.o bnxt_ptp.o bnxt_vfr.o bnxt_devlink.o bnxt_dim.o bnxt_coredump.o
bnxt_en-$(CONFIG_BNXT_FLOWER_OFFLOAD) += bnxt_tc.o
bnxt_en-$(CONFIG_DEBUG_FS) += bnxt_debugfs.o
