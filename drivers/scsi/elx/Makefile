#// SPDX-License-Identifier: GPL-2.0
#/*
# * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
# * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
# */


obj-$(CONFIG_SCSI_EFCT) := efct.o

efct-objs := efct/efct_driver.o efct/efct_io.o efct/efct_scsi.o \
	     efct/efct_xport.o efct/efct_hw.o efct/efct_hw_queues.o \
	     efct/efct_lio.o efct/efct_unsol.o

efct-objs += libefc/efc_cmds.o libefc/efc_domain.o libefc/efc_fabric.o \
	     libefc/efc_node.o libefc/efc_nport.o libefc/efc_device.o \
	     libefc/efclib.o libefc/efc_sm.o libefc/efc_els.o

efct-objs += libefc_sli/sli4.o
