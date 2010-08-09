#
# Carsten Langgaard, carstenl@mips.com
# Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
#
# Carsten Langgaard, carstenl@mips.com
# Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
# Portions copyright (C)  2009 Cisco Systems, Inc.
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
# Makefile for the Cisco PowerTV-specific kernel interface routines
# under Linux.
#

obj-y += init.o ioremap.o memory.o powertv_setup.o reset.o time.o \
	asic/ pci/

obj-$(CONFIG_USB) += powertv-usb.o

EXTRA_CFLAGS += -Wall
