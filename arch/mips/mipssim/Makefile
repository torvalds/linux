#
# Copyright (C) 2005 MIPS Technologies, Inc.  All rights reserved.
# Copyright (C) 2007 MIPS Technologies, Inc.
#   written by Ralf Baechle (ralf@linux-mips.org)
#
# This program is free software; you can distribute it and/or modify it
# under the terms of the GNU General Public License (Version 2) as
# published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
#

obj-y := sim_platform.o sim_setup.o sim_mem.o sim_time.o sim_int.o \
	 sim_cmdline.o

obj-$(CONFIG_EARLY_PRINTK) += sim_console.o
obj-$(CONFIG_SMP) += sim_smp.o
