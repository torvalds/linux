# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for Xtensa-specific library files.
#

lib-y	+= memcopy.o memset.o checksum.o \
	   ashldi3.o ashrdi3.o lshrdi3.o \
	   divsi3.o udivsi3.o modsi3.o umodsi3.o mulsi3.o \
	   usercopy.o strncpy_user.o strnlen_user.o
lib-$(CONFIG_PCI) += pci-auto.o
