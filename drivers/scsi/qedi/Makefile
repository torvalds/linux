# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_QEDI) := qedi.o
qedi-y := qedi_main.o qedi_iscsi.o qedi_fw.o qedi_sysfs.o \
	    qedi_dbg.o qedi_fw_api.o

qedi-$(CONFIG_DEBUG_FS) += qedi_debugfs.o
