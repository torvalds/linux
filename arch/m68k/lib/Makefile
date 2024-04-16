# SPDX-License-Identifier: GPL-2.0

#
# Makefile for m68k-specific library files..
#

lib-y	:= ashldi3.o ashrdi3.o lshrdi3.o muldi3.o \
	   memcpy.o memset.o memmove.o

lib-$(CONFIG_MMU) += uaccess.o
lib-$(CONFIG_CPU_HAS_NO_MULDIV64) += mulsi3.o divsi3.o udivsi3.o
lib-$(CONFIG_CPU_HAS_NO_MULDIV64) += modsi3.o umodsi3.o

ifndef CONFIG_GENERIC_CSUM
lib-y	+= checksum.o
endif

