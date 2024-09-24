#
# (C) COPYRIGHT 2018-2022 Arm Limited.
#
# This program is free software and is provided to you under the terms of the
# GNU General Public License version 2 as published by the Free Software
# Foundation, and any use by you of this program is subject to the terms
# of such GNU licence.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, you can access it online at
# http://www.gnu.org/licenses/gpl-2.0.html.
#
# SPDX-License-Identifier: GPL-2.0-only
#

ccflags-y += -Wall -Werror $(EXTRA_CCFLAGS)

obj-m := ethosn.o

ethosn-objs := ethosn_core.o \
             ethosn_backport.o \
             ethosn_buffer.o \
             ethosn_debug.o \
             ethosn_device.o \
             ethosn_dma.o \
             ethosn_dma_carveout.o \
             ethosn_dma_iommu.o \
             ethosn_driver.o \
             ethosn_network.o \
             ethosn_smc.o \
	     ethosn_asset_allocator.o \
	     ethosn_sub_allocator.o \
	     ethosn_main_allocator.o \
	     ethosn_proc_mem_allocator.o
