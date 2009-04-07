#
# Makefile for the OSD modules (out of tree)
#
# Copyright (C) 2008 Panasas Inc.  All rights reserved.
#
# Authors:
#   Boaz Harrosh <bharrosh@panasas.com>
#   Benny Halevy <bhalevy@panasas.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2
#
# This Makefile is used to call the kernel Makefile in case of an out-of-tree
# build.
# $KSRC should point to a Kernel source tree otherwise host's default is
# used. (eg. /lib/modules/`uname -r`/build)

# include path for out-of-tree Headers
OSD_INC ?= `pwd`/../../../include

# allow users to override these
# e.g. to compile for a kernel that you aren't currently running
KSRC ?= /lib/modules/$(shell uname -r)/build
KBUILD_OUTPUT ?=
ARCH ?=
V ?= 0

# this is the basic Kbuild out-of-tree invocation, with the M= option
KBUILD_BASE = +$(MAKE) -C $(KSRC) M=`pwd` KBUILD_OUTPUT=$(KBUILD_OUTPUT) ARCH=$(ARCH) V=$(V)

all: libosd

libosd: ;
	$(KBUILD_BASE) OSD_INC=$(OSD_INC) modules

clean:
	$(KBUILD_BASE) clean
