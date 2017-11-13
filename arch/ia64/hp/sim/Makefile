# SPDX-License-Identifier: GPL-2.0
#
# ia64/platform/hp/sim/Makefile
#
# Copyright (C) 2002 Hewlett-Packard Co.
#	David Mosberger-Tang <davidm@hpl.hp.com>
# Copyright (C) 1999 Silicon Graphics, Inc.
# Copyright (C) Srinivasa Thirumalachar (sprasad@engr.sgi.com)
#

obj-y := hpsim_irq.o hpsim_setup.o hpsim.o
obj-$(CONFIG_IA64_GENERIC) += hpsim_machvec.o

obj-$(CONFIG_HP_SIMETH)	+= simeth.o
obj-$(CONFIG_HP_SIMSERIAL) += simserial.o
obj-$(CONFIG_HP_SIMSERIAL_CONSOLE) += hpsim_console.o
obj-$(CONFIG_HP_SIMSCSI) += simscsi.o
