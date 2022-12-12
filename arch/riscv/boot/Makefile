#
# arch/riscv/boot/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2018, Anup Patel.
# Author: Anup Patel <anup@brainfault.org>
#
# Based on the ia64 and arm64 boot/Makefile.
#

KCOV_INSTRUMENT := n

OBJCOPYFLAGS_Image :=-O binary -R .note -R .note.gnu.build-id -R .comment -S
OBJCOPYFLAGS_xipImage :=-O binary -R .note -R .note.gnu.build-id -R .comment -S

targets := Image Image.* loader loader.o loader.lds loader.bin
targets := Image Image.* loader loader.o loader.lds loader.bin xipImage

ifeq ($(CONFIG_XIP_KERNEL),y)

quiet_cmd_mkxip = $(quiet_cmd_objcopy)
cmd_mkxip = $(cmd_objcopy)

$(obj)/xipImage: vmlinux FORCE
	$(call if_changed,mkxip)
	@$(kecho) '  Physical Address of xipImage: $(CONFIG_XIP_PHYS_ADDR)'

endif

$(obj)/Image: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/Image.gz: $(obj)/Image FORCE
	$(call if_changed,gzip)

$(obj)/loader.o: $(src)/loader.S $(obj)/Image

$(obj)/loader: $(obj)/loader.o $(obj)/Image $(obj)/loader.lds FORCE
	$(Q)$(LD) -T $(obj)/loader.lds -o $@ $(obj)/loader.o

$(obj)/Image.bz2: $(obj)/Image FORCE
	$(call if_changed,bzip2)

$(obj)/Image.lz4: $(obj)/Image FORCE
	$(call if_changed,lz4)

$(obj)/Image.lzma: $(obj)/Image FORCE
	$(call if_changed,lzma)

$(obj)/Image.lzo: $(obj)/Image FORCE
	$(call if_changed,lzo)

$(obj)/loader.bin: $(obj)/loader FORCE
	$(call if_changed,objcopy)

EFI_ZBOOT_PAYLOAD	:= Image
EFI_ZBOOT_BFD_TARGET	:= elf$(BITS)-littleriscv
EFI_ZBOOT_MACH_TYPE	:= RISCV$(BITS)

include $(srctree)/drivers/firmware/efi/libstub/Makefile.zboot
