#
# arch/arm/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995-2000 Russell King
#

SYSTEM	=$(TOPDIR)/$(LINUX)

ZTEXTADDR	 = 0x02080000
PARAMS_PHYS	 = 0x0207c000
INITRD_PHYS	 = 0x02180000
INITRD_VIRT	 = 0x02180000

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
	$(OBJCOPY) -O binary -R .note -R .comment -S vmlinux $@

#$(obj)/Image:	$(CONFIGURE) $(SYSTEM)
#	$(OBJCOPY) -O binary -R .note -R .comment -g -S $(SYSTEM) $@

bzImage: zImage

zImage:	$(CONFIGURE) compressed/$(LINUX)
	$(OBJCOPY) -O binary -R .note -R .comment -S compressed/$(LINUX) $@

bootpImage: bootp/bootp
	$(OBJCOPY) -O binary -R .note -R .comment -S bootp/bootp $@

compressed/$(LINUX): $(TOPDIR)/$(LINUX) dep
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
	sh ./install.sh $(KERNELRELEASE) Image $(TOPDIR)/System.map "$(INSTALL_PATH)"

zinstall: $(CONFIGURE) zImage
	sh ./install.sh $(KERNELRELEASE) zImage $(TOPDIR)/System.map "$(INSTALL_PATH)"

#
# miscellany
#
mrproper clean:
	$(RM) Image zImage bootpImage
#	@$(MAKE) -C compressed clean
#	@$(MAKE) -C bootp clean

dep:
