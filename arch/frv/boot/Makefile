#
# arch/arm/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995-2000 Russell King
#

targets := Image zImage bootpImage

SYSTEM	=$(LINUX)

ZTEXTADDR	 = 0x02080000
PARAMS_PHYS	 = 0x0207c000
INITRD_PHYS	 = 0x02180000
INITRD_VIRT	 = 0x02180000

OBJCOPYFLAGS	:=-O binary -R .note -R .note.gnu.build-id -R .comment

#
# If you don't define ZRELADDR above,
# then it defaults to ZTEXTADDR
#
ifeq ($(ZRELADDR),)
ZRELADDR	= $(ZTEXTADDR)
endif

export	SYSTEM ZTEXTADDR ZBSSADDR ZRELADDR INITRD_PHYS INITRD_VIRT PARAMS_PHYS

Image: $(obj)/Image

targets: $(obj)/Image

$(obj)/Image: vmlinux FORCE
	$(OBJCOPY) $(OBJCOPYFLAGS) -S vmlinux $@

#$(obj)/Image:	$(CONFIGURE) $(SYSTEM)
#	$(OBJCOPY) $(OBJCOPYFLAGS) -g -S $(SYSTEM) $@

bzImage: zImage

zImage:	$(CONFIGURE) compressed/$(LINUX)
	$(OBJCOPY) $(OBJCOPYFLAGS) -S compressed/$(LINUX) $@

bootpImage: bootp/bootp
	$(OBJCOPY) $(OBJCOPYFLAGS) -S bootp/bootp $@

compressed/$(LINUX): $(LINUX) dep
	@$(MAKE) -C compressed $(LINUX)

bootp/bootp: zImage initrd
	@$(MAKE) -C bootp bootp

initrd:
	@test "$(INITRD_VIRT)" != "" || (echo This architecture does not support INITRD; exit -1)
	@test "$(INITRD)" != "" || (echo You must specify INITRD; exit -1)

#
# installation
#
install: $(CONFIGURE) Image
	sh ./install.sh $(KERNELRELEASE) Image System.map "$(INSTALL_PATH)"

zinstall: $(CONFIGURE) zImage
	sh ./install.sh $(KERNELRELEASE) zImage System.map "$(INSTALL_PATH)"

#
# miscellany
#
mrproper clean:
#	@$(MAKE) -C compressed clean
#	@$(MAKE) -C bootp clean

dep:
