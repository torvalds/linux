# SPDX-License-Identifier: GPL-2.0
# Makefile for comedi drivers unit tests
#
ccflags-$(CONFIG_COMEDI_DEBUG)		:= -DDEBUG

obj-$(CONFIG_COMEDI_TESTS)		+= comedi_example_test.o
obj-$(CONFIG_COMEDI_TESTS)		+= ni_routes_test.o
CFLAGS_ni_routes_test.o			:= -DDEBUG
