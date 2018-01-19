# SPDX-License-Identifier: GPL-2.0
#
# Makefile for bootable kernel images
#

OBJCOPYFLAGS_vmlinux.bin := -O binary
$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/dtbImage.%: vmlinux
	$(call if_changed,objcopy)
