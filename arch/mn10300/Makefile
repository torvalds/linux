###############################################################################
#
# MN10300 Kernel makefile system specifications
#
# Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
# Modified by David Howells (dhowells@redhat.com)
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public Licence
# as published by the Free Software Foundation; either version
# 2 of the Licence, or (at your option) any later version.
#
###############################################################################

KBUILD_DEFCONFIG := asb2303_defconfig

CCSPECS	:= $(shell $(CC) -v 2>&1 | grep "^Reading specs from " | head -1 | cut -c20-)
CCDIR	:= $(strip $(patsubst %/specs,%,$(CCSPECS)))
KBUILD_CPPFLAGS += -nostdinc -I$(CCDIR)/include

LDFLAGS		:=
OBJCOPYFLAGS	:= -O binary -R .note -R .comment -R .GCC-command-line -R .note.gnu.build-id -S
#LDFLAGS_vmlinux := -Map linkmap.txt
CHECKFLAGS	+=

PROCESSOR	:= unset
UNIT		:= unset

KBUILD_CFLAGS	+= -mam33 -mmem-funcs -DCPU=AM33
KBUILD_AFLAGS	+= -mam33 -DCPU=AM33

ifeq ($(CONFIG_MN10300_CURRENT_IN_E2),y)
KBUILD_CFLAGS	+= -ffixed-e2 -fcall-saved-e5
endif

ifeq ($(CONFIG_MN10300_PROC_MN103E010),y)
PROCESSOR	:= mn103e010
endif
ifeq ($(CONFIG_MN10300_PROC_MN2WS0050),y)
PROCESSOR	:= mn2ws0050
endif

ifeq ($(CONFIG_MN10300_UNIT_ASB2303),y)
UNIT		:= asb2303
endif
ifeq ($(CONFIG_MN10300_UNIT_ASB2305),y)
UNIT		:= asb2305
endif
ifeq ($(CONFIG_MN10300_UNIT_ASB2364),y)
UNIT		:= asb2364
endif


head-y		:= arch/mn10300/kernel/head.o arch/mn10300/kernel/init_task.o

core-y		+= arch/mn10300/kernel/ arch/mn10300/mm/

ifneq ($(PROCESSOR),unset)
core-y		+= arch/mn10300/proc-$(PROCESSOR)/
endif
ifneq ($(UNIT),unset)
core-y		+= arch/mn10300/unit-$(UNIT)/
endif
libs-y		+= arch/mn10300/lib/

drivers-$(CONFIG_OPROFILE)	+= arch/mn10300/oprofile/

boot := arch/mn10300/boot

.PHONY: zImage

KBUILD_IMAGE := $(boot)/zImage
CLEAN_FILES += $(boot)/zImage
CLEAN_FILES += $(boot)/compressed/vmlinux
CLEAN_FILES += $(boot)/compressed/vmlinux.bin
CLEAN_FILES += $(boot)/compressed/vmlinux.bin.gz

zImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

all: zImage

bootstrap:
	$(Q)$(MAKEBOOT) bootstrap

archclean:
	$(Q)$(MAKE) $(clean)=arch/mn10300/proc-mn103e010
	$(Q)$(MAKE) $(clean)=arch/mn10300/unit-asb2303
	$(Q)$(MAKE) $(clean)=arch/mn10300/unit-asb2305

define archhelp
  echo  '* zImage        - Compressed kernel image (arch/$(ARCH)/boot/zImage)'
endef

# If you make sure the .S files get compiled with debug info,
# uncomment the following to disable optimisations
# that are unhelpful whilst debugging.
ifdef CONFIG_DEBUG_INFO
#KBUILD_CFLAGS	+= -O1
KBUILD_AFLAGS	+= -Wa,--gdwarf2
endif

#
# include the appropriate processor- and unit-specific headers
#
KBUILD_CPPFLAGS += -I$(srctree)/arch/mn10300/proc-$(PROCESSOR)/include
KBUILD_CPPFLAGS += -I$(srctree)/arch/mn10300/unit-$(UNIT)/include
