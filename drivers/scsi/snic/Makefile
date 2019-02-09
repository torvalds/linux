# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_SCSI_SNIC) += snic.o

snic-y := \
	snic_attrs.o \
	snic_main.o \
	snic_res.o \
	snic_isr.o \
	snic_ctl.o \
	snic_io.o \
	snic_scsi.o \
	snic_disc.o \
	vnic_cq.o \
	vnic_intr.o \
	vnic_dev.o \
	vnic_wq.o

snic-$(CONFIG_SCSI_SNIC_DEBUG_FS) += snic_debugfs.o snic_trc.o
