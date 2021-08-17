# SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
# Copyright (c) 2019-2020 Marvell International Ltd.

obj-$(CONFIG_QED) := qed.o

qed-y :=			\
	qed_chain.o		\
	qed_cxt.o		\
	qed_dcbx.o		\
	qed_debug.o		\
	qed_dev.o		\
	qed_devlink.o		\
	qed_hw.o		\
	qed_init_fw_funcs.o	\
	qed_init_ops.o		\
	qed_int.o		\
	qed_l2.o		\
	qed_main.o		\
	qed_mcp.o		\
	qed_mng_tlv.o		\
	qed_ptp.o		\
	qed_selftest.o		\
	qed_sp_commands.o	\
	qed_spq.o

qed-$(CONFIG_QED_FCOE) += qed_fcoe.o
qed-$(CONFIG_QED_ISCSI) += qed_iscsi.o
qed-$(CONFIG_QED_LL2) += qed_ll2.o
qed-$(CONFIG_QED_OOO) += qed_ooo.o

qed-$(CONFIG_QED_NVMETCP) +=	\
	qed_nvmetcp.o		\
	qed_nvmetcp_fw_funcs.o	\
	qed_nvmetcp_ip_services.o

qed-$(CONFIG_QED_RDMA) +=	\
	qed_iwarp.o		\
	qed_rdma.o		\
	qed_roce.o

qed-$(CONFIG_QED_SRIOV) +=	\
	qed_sriov.o		\
	qed_vf.o
