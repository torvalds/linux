#
# Makefile for the Cavium Octeon specific kernel interface routines
# under Linux.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2005-2009 Cavium Networks
#

obj-y := cpu.o setup.o octeon-platform.o octeon-irq.o csrc-octeon.o
obj-y += dma-octeon.o
obj-y += octeon-crypto.o
obj-y += octeon-memcpy.o
obj-y += executive/

obj-$(CONFIG_MTD)		      += flash_setup.o
obj-$(CONFIG_SMP)		      += smp.o
obj-$(CONFIG_OCTEON_ILM)	      += oct_ilm.o
