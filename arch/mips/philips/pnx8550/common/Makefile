#
# Per Hallsmark, per.hallsmark@mvista.com
#
# ########################################################################
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
# #######################################################################
#
# Makefile for the PNX8550 specific kernel interface routines
# under Linux.
#

obj-y := setup.o prom.o int.o reset.o time.o proc.o platform.o
obj-$(CONFIG_PCI) += pci.o
obj-$(CONFIG_KGDB) += gdb_hook.o
