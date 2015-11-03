$(obj)/qat_rsakey-asn1.o: $(obj)/qat_rsakey-asn1.c $(obj)/qat_rsakey-asn1.h
clean-files += qat_rsakey-asn1.c qat_rsakey-asn1.h

obj-$(CONFIG_CRYPTO_DEV_QAT) += intel_qat.o
intel_qat-objs := adf_cfg.o \
	adf_ctl_drv.o \
	adf_dev_mgr.o \
	adf_init.o \
	adf_accel_engine.o \
	adf_aer.o \
	adf_transport.o \
	adf_admin.o \
	adf_hw_arbiter.o \
	qat_crypto.o \
	qat_algs.o \
	qat_rsakey-asn1.o \
	qat_asym_algs.o \
	qat_uclo.o \
	qat_hal.o

intel_qat-$(CONFIG_DEBUG_FS) += adf_transport_debug.o
intel_qat-$(CONFIG_PCI_IOV) += adf_sriov.o adf_pf2vf_msg.o
