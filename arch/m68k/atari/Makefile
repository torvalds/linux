# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for Linux arch/m68k/atari source directory
#

obj-y		:= config.o time.o debug.o ataints.o stdma.o \
			atasound.o stram.o

obj-$(CONFIG_ATARI_KBD_CORE)	+= atakeyb.o

obj-$(CONFIG_NVRAM:m=y)		+= nvram.o
