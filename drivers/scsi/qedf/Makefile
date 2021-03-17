# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_QEDF) := qedf.o
qedf-y = qedf_dbg.o qedf_main.o qedf_io.o qedf_fip.o \
	 qedf_attr.o qedf_els.o drv_scsi_fw_funcs.o drv_fcoe_fw_funcs.o

qedf-$(CONFIG_DEBUG_FS) += qedf_debugfs.o
