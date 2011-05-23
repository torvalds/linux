#
# Makefile for Adaptec aic94xx SAS/SATA driver.
#
# Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
# Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
#
# This file is licensed under GPLv2.
#
# This file is part of the aic94xx driver.
#
# The aic94xx driver is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; version 2 of the
# License.
#
# The aic94xx driver is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with the aic94xx driver; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

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
