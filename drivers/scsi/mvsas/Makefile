# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for Marvell 88SE64xx/88SE84xx SAS/SATA driver.
#
# Copyright 2007 Red Hat, Inc.
# Copyright 2008 Marvell. <kewei@marvell.com>
# Copyright 2009-2011 Marvell. <yuxiangl@marvell.com>
#

ccflags-$(CONFIG_SCSI_MVSAS_DEBUG) := -DMV_DEBUG

obj-$(CONFIG_SCSI_MVSAS) += mvsas.o
mvsas-y +=  mv_init.o  \
           mv_sas.o   \
           mv_64xx.o  \
           mv_94xx.o
