# BK Id: %F% %I% %G% %U% %#%
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
# Modifications for the OpenRISC architecture:
# Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
# Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
#
# Based on:
# arch/i386/Makefile

KBUILD_DEFCONFIG := or1ksim_defconfig

OBJCOPYFLAGS    := -O binary -R .note -R .comment -S
LIBGCC 		:= $(shell $(CC) $(KBUILD_CFLAGS) -print-libgcc-file-name)

KBUILD_CFLAGS	+= -pipe -ffixed-r10 -D__linux__
KBUILD_CFLAGS	+= -msfimm -mshftimm

all: vmlinux.bin

boot := arch/$(ARCH)/boot

ifeq ($(CONFIG_OPENRISC_HAVE_INST_MUL),y)
	KBUILD_CFLAGS += $(call cc-option,-mhard-mul)
else
	KBUILD_CFLAGS += $(call cc-option,-msoft-mul)
endif

ifeq ($(CONFIG_OPENRISC_HAVE_INST_DIV),y)
	KBUILD_CFLAGS += $(call cc-option,-mhard-div)
else
	KBUILD_CFLAGS += $(call cc-option,-msoft-div)
endif

ifeq ($(CONFIG_OPENRISC_HAVE_INST_CMOV),y)
	KBUILD_CFLAGS += $(call cc-option,-mcmov)
endif

ifeq ($(CONFIG_OPENRISC_HAVE_INST_ROR),y)
	KBUILD_CFLAGS += $(call cc-option,-mror)
endif

ifeq ($(CONFIG_OPENRISC_HAVE_INST_RORI),y)
	KBUILD_CFLAGS += $(call cc-option,-mrori)
endif

ifeq ($(CONFIG_OPENRISC_HAVE_INST_SEXT),y)
	KBUILD_CFLAGS += $(call cc-option,-msext)
endif

libs-y		+= $(LIBGCC)

PHONY += vmlinux.bin

vmlinux.bin: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@
