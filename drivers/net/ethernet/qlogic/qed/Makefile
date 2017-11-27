# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_QED) := qed.o

qed-y := qed_cxt.o qed_dev.o qed_hw.o qed_init_fw_funcs.o qed_init_ops.o \
	 qed_int.o qed_main.o qed_mcp.o qed_sp_commands.o qed_spq.o qed_l2.o \
	 qed_selftest.o qed_dcbx.o qed_debug.o qed_ptp.o
qed-$(CONFIG_QED_SRIOV) += qed_sriov.o qed_vf.o
qed-$(CONFIG_QED_LL2) += qed_ll2.o
qed-$(CONFIG_QED_RDMA) += qed_roce.o qed_rdma.o qed_iwarp.o
qed-$(CONFIG_QED_ISCSI) += qed_iscsi.o
qed-$(CONFIG_QED_FCOE) += qed_fcoe.o
qed-$(CONFIG_QED_OOO) += qed_ooo.o
