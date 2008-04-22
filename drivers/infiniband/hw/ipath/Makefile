EXTRA_CFLAGS += -DIPATH_IDSTR='"QLogic kernel.org driver"' \
	-DIPATH_KERN_TYPE=0

obj-$(CONFIG_INFINIBAND_IPATH) += ib_ipath.o

ib_ipath-y := \
	ipath_cq.o \
	ipath_diag.o \
	ipath_dma.o \
	ipath_driver.o \
	ipath_eeprom.o \
	ipath_file_ops.o \
	ipath_fs.o \
	ipath_init_chip.o \
	ipath_intr.o \
	ipath_keys.o \
	ipath_mad.o \
	ipath_mmap.o \
	ipath_mr.o \
	ipath_qp.o \
	ipath_rc.o \
	ipath_ruc.o \
	ipath_sdma.o \
	ipath_srq.o \
	ipath_stats.o \
	ipath_sysfs.o \
	ipath_uc.o \
	ipath_ud.o \
	ipath_user_pages.o \
	ipath_user_sdma.o \
	ipath_verbs_mcast.o \
	ipath_verbs.o \
	ipath_iba7220.o \
	ipath_sd7220.o \
	ipath_sd7220_img.o

ib_ipath-$(CONFIG_HT_IRQ) += ipath_iba6110.o
ib_ipath-$(CONFIG_PCI_MSI) += ipath_iba6120.o

ib_ipath-$(CONFIG_X86_64) += ipath_wc_x86_64.o
ib_ipath-$(CONFIG_PPC64) += ipath_wc_ppc64.o
