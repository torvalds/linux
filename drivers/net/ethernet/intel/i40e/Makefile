################################################################################
#
# Intel Ethernet Controller XL710 Family Linux Driver
# Copyright(c) 2013 - 2015 Intel Corporation.
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
# You should have received a copy of the GNU General Public License along
# with this program.  If not, see <http://www.gnu.org/licenses/>.
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
# Makefile for the Intel(R) Ethernet Connection XL710 (i40e.ko) driver
#

ccflags-y += -I$(src)
subdir-ccflags-y += -I$(src)

obj-$(CONFIG_I40E) += i40e.o

i40e-objs := i40e_main.o \
	i40e_ethtool.o	\
	i40e_adminq.o	\
	i40e_common.o	\
	i40e_hmc.o	\
	i40e_lan_hmc.o	\
	i40e_nvm.o	\
	i40e_debugfs.o	\
	i40e_diag.o	\
	i40e_txrx.o	\
	i40e_ptp.o	\
	i40e_client.o   \
	i40e_virtchnl_pf.o

i40e-$(CONFIG_I40E_DCB) += i40e_dcb.o i40e_dcb_nl.o
