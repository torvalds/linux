# BK Id: %F% %I% %G% %U% %#%
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture
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

LDFLAGS         :=
OBJCOPYFLAGS    := -O binary -R .note -R .comment -S
LDFLAGS_vmlinux :=
LIBGCC 		:= $(shell $(CC) $(KBUILD_CFLAGS) -print-libgcc-file-name)

KBUILD_CFLAGS	+= -pipe -ffixed-r10

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

head-y 		:= arch/openrisc/kernel/head.o arch/openrisc/kernel/init_task.o

core-y		+= arch/openrisc/lib/ \
		   arch/openrisc/kernel/ \
		   arch/openrisc/mm/
libs-y		+= $(LIBGCC)

ifneq '$(CONFIG_OPENRISC_BUILTIN_DTB)' '""'
BUILTIN_DTB := y
else
BUILTIN_DTB := n
endif
core-$(BUILTIN_DTB) += arch/openrisc/boot/

all: vmlinux
