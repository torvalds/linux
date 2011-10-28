# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2005-2009 Cavium Networks
#

#
# Makefile for Cavium OCTEON on-board ethernet driver
#

obj-${CONFIG_OCTEON_ETHERNET} :=  octeon-ethernet.o

octeon-ethernet-y := ethernet.o
octeon-ethernet-y += ethernet-mdio.o
octeon-ethernet-y += ethernet-mem.o
octeon-ethernet-y += ethernet-rgmii.o
octeon-ethernet-y += ethernet-rx.o
octeon-ethernet-y += ethernet-sgmii.o
octeon-ethernet-y += ethernet-spi.o
octeon-ethernet-y += ethernet-tx.o
octeon-ethernet-y += ethernet-xaui.o
octeon-ethernet-y += cvmx-pko.o cvmx-spi.o cvmx-cmd-queue.o \
	cvmx-helper-board.o cvmx-helper.o cvmx-helper-xaui.o \
	cvmx-helper-rgmii.o cvmx-helper-sgmii.o cvmx-helper-npi.o \
	cvmx-helper-loop.o cvmx-helper-spi.o cvmx-helper-util.o \
	cvmx-interrupt-decodes.o cvmx-interrupt-rsl.o

