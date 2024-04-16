# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the kernel PNPBIOS driver.
#
obj-y := pnp.o

pnp-y := core.o bioscalls.o rsparser.o

pnp-$(CONFIG_PNPBIOS_PROC_FS) += proc.o
