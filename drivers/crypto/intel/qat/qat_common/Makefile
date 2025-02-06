# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_CRYPTO_DEV_QAT) += intel_qat.o
ccflags-y += -DDEFAULT_SYMBOL_NAMESPACE='"CRYPTO_QAT"'
intel_qat-objs := adf_cfg.o \
	adf_isr.o \
	adf_ctl_drv.o \
	adf_cfg_services.o \
	adf_dev_mgr.o \
	adf_init.o \
	adf_accel_engine.o \
	adf_aer.o \
	adf_transport.o \
	adf_admin.o \
	adf_hw_arbiter.o \
	adf_sysfs.o \
	adf_sysfs_ras_counters.o \
	adf_gen2_hw_csr_data.o \
	adf_gen2_hw_data.o \
	adf_gen2_config.o \
	adf_gen4_config.o \
	adf_gen4_hw_csr_data.o \
	adf_gen4_hw_data.o \
	adf_gen4_vf_mig.o \
	adf_gen4_pm.o \
	adf_gen2_dc.o \
	adf_gen4_dc.o \
	adf_gen4_ras.o \
	adf_gen4_timer.o \
	adf_clock.o \
	adf_mstate_mgr.o \
	qat_crypto.o \
	qat_compression.o \
	qat_comp_algs.o \
	qat_algs.o \
	qat_asym_algs.o \
	qat_algs_send.o \
	adf_rl.o \
	adf_rl_admin.o \
	adf_sysfs_rl.o \
	qat_uclo.o \
	qat_hal.o \
	qat_bl.o \
	qat_mig_dev.o

intel_qat-$(CONFIG_DEBUG_FS) += adf_transport_debug.o \
				adf_fw_counters.o \
				adf_cnv_dbgfs.o \
				adf_gen4_pm_debugfs.o \
				adf_gen4_tl.o \
				adf_heartbeat.o \
				adf_heartbeat_dbgfs.o \
				adf_pm_dbgfs.o \
				adf_telemetry.o \
				adf_tl_debugfs.o \
				adf_dbgfs.o

intel_qat-$(CONFIG_PCI_IOV) += adf_sriov.o adf_vf_isr.o adf_pfvf_utils.o \
			       adf_pfvf_pf_msg.o adf_pfvf_pf_proto.o \
			       adf_pfvf_vf_msg.o adf_pfvf_vf_proto.o \
			       adf_gen2_pfvf.o adf_gen4_pfvf.o

intel_qat-$(CONFIG_CRYPTO_DEV_QAT_ERROR_INJECTION) += adf_heartbeat_inject.o
