# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_CRYPTO_DEV_QAT) += intel_qat.o
ccflags-y += -DDEFAULT_SYMBOL_NAMESPACE='"CRYPTO_QAT"'
intel_qat-y := adf_accel_engine.o \
	adf_admin.o \
	adf_aer.o \
	adf_bank_state.o \
	adf_cfg.o \
	adf_cfg_services.o \
	adf_clock.o \
	adf_ctl_drv.o \
	adf_dc.o \
	adf_dev_mgr.o \
	adf_gen2_config.o \
	adf_gen2_hw_csr_data.o \
	adf_gen2_hw_data.o \
	adf_gen4_config.o \
	adf_gen4_hw_csr_data.o \
	adf_gen4_hw_data.o \
	adf_gen4_pm.o \
	adf_gen4_ras.o \
	adf_gen4_vf_mig.o \
	adf_gen6_ras.o \
	adf_gen6_shared.o \
	adf_hw_arbiter.o \
	adf_init.o \
	adf_isr.o \
	adf_mstate_mgr.o \
	adf_rl_admin.o \
	adf_rl.o \
	adf_sysfs.o \
	adf_sysfs_ras_counters.o \
	adf_sysfs_rl.o \
	adf_timer.o \
	adf_transport.o \
	qat_algs.o \
	qat_algs_send.o \
	qat_asym_algs.o \
	qat_bl.o \
	qat_comp_algs.o \
	qat_compression.o \
	qat_crypto.o \
	qat_hal.o \
	qat_mig_dev.o \
	qat_uclo.o

intel_qat-$(CONFIG_DEBUG_FS) += adf_cnv_dbgfs.o \
				adf_dbgfs.o \
				adf_fw_counters.o \
				adf_gen4_pm_debugfs.o \
				adf_gen4_tl.o \
				adf_gen6_pm_dbgfs.o \
				adf_gen6_tl.o \
				adf_heartbeat_dbgfs.o \
				adf_heartbeat.o \
				adf_pm_dbgfs.o \
				adf_pm_dbgfs_utils.o \
				adf_telemetry.o \
				adf_tl_debugfs.o \
				adf_transport_debug.o

intel_qat-$(CONFIG_PCI_IOV) += adf_gen2_pfvf.o adf_gen4_pfvf.o \
			       adf_pfvf_pf_msg.o adf_pfvf_pf_proto.o \
			       adf_pfvf_utils.o adf_pfvf_vf_msg.o \
			       adf_pfvf_vf_proto.o adf_sriov.o adf_vf_isr.o

intel_qat-$(CONFIG_CRYPTO_DEV_QAT_ERROR_INJECTION) += adf_heartbeat_inject.o
