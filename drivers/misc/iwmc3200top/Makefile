# iwmc3200top - Intel Wireless MultiCom 3200 Top Driver
# drivers/misc/iwmc3200top/Makefile
#
# Copyright (C) 2009 Intel Corporation. All rights reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License version
# 2 as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.
#
#
# Author Name: Maxim Grabarnik <maxim.grabarnink@intel.com>
#  -
#
#

obj-$(CONFIG_IWMC3200TOP)	+= iwmc3200top.o
iwmc3200top-objs	:= main.o fw-download.o
iwmc3200top-$(CONFIG_IWMC3200TOP_DEBUG) += log.o
iwmc3200top-$(CONFIG_IWMC3200TOP_DEBUGFS) += debugfs.o
