EXTRA_CFLAGS += -DIPATH_IDSTR='"PathScale kernel.org driver"' \
	-DIPATH_KERN_TYPE=0

obj-$(CONFIG_IPATH_CORE) += ipath_core.o
obj-$(CONFIG_INFINIBAND_IPATH) += ib_ipath.o

ipath_core-y := \
	ipath_diag.o \
	ipath_driver.o \
	ipath_eeprom.o \
	ipath_file_ops.o \
	ipath_fs.o \
	ipath_ht400.o \
	ipath_init_chip.o \
	ipath_intr.o \
	ipath_layer.o \
	ipath_pe800.o \
	ipath_stats.o \
	ipath_sysfs.o \
	ipath_user_pages.o

ipath_core-$(CONFIG_X86_64) += ipath_wc_x86_64.o

ib_ipath-y := \
	ipath_cq.o \
	ipath_keys.o \
	ipath_mad.o \
	ipath_mr.o \
	ipath_qp.o \
	ipath_rc.o \
	ipath_ruc.o \
	ipath_srq.o \
	ipath_uc.o \
	ipath_ud.o \
	ipath_verbs.o \
	ipath_verbs_mcast.o
