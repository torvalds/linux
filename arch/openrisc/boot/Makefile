# SPDX-License-Identifier: GPL-2.0
#
# Makefile for bootable kernel images
#

targets += vmlinux.bin

OBJCOPYFLAGS_vmlinux.bin := -O binary
$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)
