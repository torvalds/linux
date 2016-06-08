obj-$(CONFIG_QED) := qed.o

qed-y := qed_cxt.o qed_dev.o qed_hw.o qed_init_fw_funcs.o qed_init_ops.o \
	 qed_int.o qed_main.o qed_mcp.o qed_sp_commands.o qed_spq.o qed_l2.o \
	 qed_selftest.o qed_dcbx.o
qed-$(CONFIG_QED_SRIOV) += qed_sriov.o qed_vf.o
