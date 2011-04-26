#
# Makefile for Marvell 88SE64xx/88SE84xx SAS/SATA driver.
#
# Copyright 2007 Red Hat, Inc.
# Copyright 2008 Marvell. <kewei@marvell.com>
# Copyright 2009-2011 Marvell. <yuxiangl@marvell.com>
#
# This file is licensed under GPLv2.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; version 2 of the
# License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
# USA

ccflags-$(CONFIG_SCSI_MVSAS_DEBUG) := -DMV_DEBUG

obj-$(CONFIG_SCSI_MVSAS) += mvsas.o
mvsas-y +=  mv_init.o  \
           mv_sas.o   \
           mv_64xx.o  \
           mv_94xx.o
