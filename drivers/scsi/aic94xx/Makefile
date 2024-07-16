# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for Adaptec aic94xx SAS/SATA driver.
#
# Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
# Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
#

ccflags-$(CONFIG_AIC94XX_DEBUG) := -DASD_DEBUG -DASD_ENTER_EXIT

obj-$(CONFIG_SCSI_AIC94XX) += aic94xx.o
aic94xx-y += aic94xx_init.o \
	     aic94xx_hwi.o  \
	     aic94xx_reg.o  \
	     aic94xx_sds.o  \
	     aic94xx_seq.o  \
	     aic94xx_dump.o \
	     aic94xx_scb.o  \
	     aic94xx_dev.o  \
	     aic94xx_tmf.o  \
	     aic94xx_task.o
