# SPDX-License-Identifier: GPL-2.0

KBUILD_AFLAGS		+= -D__DISABLE_EXPORTS

lib-$(CONFIG_X86_64)		+= la57toggle.o
lib-$(CONFIG_EFI_MIXED)		+= efi-mixed.o

#
# Disable objtool validation for all library code, which is intended
# to be linked into the decompressor or the EFI stub but not vmlinux
#
$(patsubst %.o,$(obj)/%.o,$(lib-y)): OBJECT_FILES_NON_STANDARD := y
