#
# arch/ppc/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
# Adapted for PowerPC by Gary Thomas
# modified by Cort (cort@cs.nmt.edu)
#

CFLAGS	 	+= -fno-builtin -D__BOOTER__ -Iarch/$(ARCH)/boot/include
HOSTCFLAGS	+= -Iarch/$(ARCH)/boot/include

BOOT_TARGETS	= zImage zImage.initrd znetboot znetboot.initrd

bootdir-y			:= simple
bootdir-$(CONFIG_PPC_OF)	+= openfirmware
subdir-y			:= lib common images
subdir-$(CONFIG_PPC_OF)		+= of1275

# for cleaning
subdir-				+= simple openfirmware

hostprogs-y := $(addprefix utils/, addnote mknote hack-coff mkprep mkbugboot mktree)

.PHONY: $(BOOT_TARGETS) $(bootdir-y)

$(BOOT_TARGETS): $(bootdir-y)

$(bootdir-y): $(addprefix $(obj)/,$(subdir-y)) \
		$(addprefix $(obj)/,$(hostprogs-y))
	$(Q)$(MAKE) $(build)=$(obj)/$@ $(MAKECMDGOALS)
