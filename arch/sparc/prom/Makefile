# SPDX-License-Identifier: GPL-2.0
# Makefile for the Sun Boot PROM interface library under
# Linux.
#

lib-y                 := bootstr_$(BITS).o
lib-y                 += init_$(BITS).o
lib-$(CONFIG_SPARC32) += memory.o
lib-y                 += misc_$(BITS).o
lib-$(CONFIG_SPARC32) += mp.o
lib-$(CONFIG_SPARC32) += ranges.o
lib-y                 += console_$(BITS).o
lib-y                 += printf.o
lib-y                 += tree_$(BITS).o
lib-$(CONFIG_SPARC64) += p1275.o
lib-$(CONFIG_SPARC64) += cif.o
