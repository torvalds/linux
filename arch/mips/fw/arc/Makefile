# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the ARC prom monitor library routines under Linux.
#

lib-y				+= cmdline.o env.o file.o identify.o init.o \
				   misc.o salone.o time.o tree.o

lib-$(CONFIG_ARC_MEMORY)	+= memory.o
lib-$(CONFIG_ARC_CONSOLE)	+= arc_con.o
lib-$(CONFIG_ARC_PROMLIB)	+= promlib.o
