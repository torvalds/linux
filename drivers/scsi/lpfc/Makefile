#/*******************************************************************
# * This file is part of the Emulex Linux Device Driver for         *
# * Enterprise Fibre Channel Host Bus Adapters.                     *
# * Refer to the README file included with this package for         *
# * driver version and adapter support.                             *
# * Copyright (C) 2004 Emulex Corporation.                          *
# * www.emulex.com                                                  *
# *                                                                 *
# * This program is free software; you can redistribute it and/or   *
# * modify it under the terms of the GNU General Public License     *
# * as published by the Free Software Foundation; either version 2  *
# * of the License, or (at your option) any later version.          *
# *                                                                 *
# * This program is distributed in the hope that it will be useful, *
# * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
# * GNU General Public License for more details, a copy of which    *
# * can be found in the file COPYING included with this package.    *
# *******************************************************************/
######################################################################

#$Id: Makefile 1.58 2005/01/23 19:00:32EST sf_support Exp  $

ifneq ($(GCOV),)
  EXTRA_CFLAGS += -fprofile-arcs -ftest-coverage
  EXTRA_CFLAGS += -O0
endif

obj-$(CONFIG_SCSI_LPFC) := lpfc.o

lpfc-objs := lpfc_mem.o lpfc_sli.o lpfc_ct.o lpfc_els.o lpfc_hbadisc.o	\
	lpfc_init.o lpfc_mbox.o lpfc_nportdisc.o lpfc_scsi.o lpfc_attr.o
