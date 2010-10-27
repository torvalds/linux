obj-$(CONFIG_INFINIBAND_QIB) += ib_qib.o

ib_qib-y := qib_cq.o qib_diag.o qib_dma.o qib_driver.o qib_eeprom.o \
	qib_file_ops.o qib_fs.o qib_init.o qib_intr.o qib_keys.o \
	qib_mad.o qib_mmap.o qib_mr.o qib_pcie.o qib_pio_copy.o \
	qib_qp.o qib_qsfp.o qib_rc.o qib_ruc.o qib_sdma.o qib_srq.o \
	qib_sysfs.o qib_twsi.o qib_tx.o qib_uc.o qib_ud.o \
	qib_user_pages.o qib_user_sdma.o qib_verbs_mcast.o qib_iba7220.o \
	qib_sd7220.o qib_iba7322.o qib_verbs.o

# 6120 has no fallback if no MSI interrupts, others can do INTx
ib_qib-$(CONFIG_PCI_MSI) += qib_iba6120.o

ib_qib-$(CONFIG_X86_64) += qib_wc_x86_64.o
ib_qib-$(CONFIG_PPC64) += qib_wc_ppc64.o
