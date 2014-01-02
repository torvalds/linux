################################################################################
#
# Intel Ethernet Controller XL710 Family Linux Virtual Function Driver
# Copyright(c) 2013 Intel Corporation.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# The full GNU General Public License is included in this distribution in
# the file called "COPYING".
#
# Contact Information:
# e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
# Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
#
################################################################################

#
## Makefile for the Intel(R) 40GbE VF driver
#
#

obj-$(CONFIG_I40EVF) += i40evf.o

i40evf-objs :=	i40evf_main.o i40evf_ethtool.o i40evf_virtchnl.o \
		i40e_txrx.o i40e_common.o i40e_adminq.o

