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

CFLAGS_octeon-platform.o = -I$(src)/../../../scripts/dtc/libfdt
CFLAGS_setup.o = -I$(src)/../../../scripts/dtc/libfdt

obj-y := cpu.o setup.o serial.o octeon-platform.o octeon-irq.o csrc-octeon.o
obj-y += dma-octeon.o
obj-y += octeon-memcpy.o
obj-y += executive/

obj-$(CONFIG_MTD)		      += flash_setup.o
obj-$(CONFIG_SMP)		      += smp.o
obj-$(CONFIG_OCTEON_ILM)	      += oct_ilm.o

DTS_FILES = octeon_3xxx.dts octeon_68xx.dts
DTB_FILES = $(patsubst %.dts, %.dtb, $(DTS_FILES))

obj-y += $(patsubst %.dts, %.dtb.o, $(DTS_FILES))

# Let's keep the .dtb files around in case we want to look at them.
.SECONDARY:  $(addprefix $(obj)/, $(DTB_FILES))

clean-files += $(DTB_FILES) $(patsubst %.dtb, %.dtb.S, $(DTB_FILES))
