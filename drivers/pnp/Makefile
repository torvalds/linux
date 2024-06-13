# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux Plug-and-Play Support.
#

obj-y		:= pnp.o

pnp-y		:= core.o card.o driver.o resource.o manager.o support.o interface.o quirks.o

obj-$(CONFIG_PNPACPI)		+= pnpacpi/
obj-$(CONFIG_PNPBIOS)		+= pnpbios/
obj-$(CONFIG_ISAPNP)		+= isapnp/

# pnp_system_init goes after pnpacpi/pnpbios init
pnp-y				+= system.o
