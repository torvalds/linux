# $Id: Makefile,v 1.23 2004/10/19 13:07:34 starvik Exp $
# cris/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.

# A bug in ld prevents us from having a (constant-value) symbol in a
# "ORIGIN =" or "LENGTH =" expression.

arch-y := v10
arch-$(CONFIG_ETRAX_ARCH_V10) := v10

# No config avaiable for make clean etc
ifneq ($(arch-y),)
SARCH := arch-$(arch-y)
else
SARCH :=
endif

LD = $(CROSS_COMPILE)ld -mcrislinux

OBJCOPYFLAGS := -O binary -R .note -R .comment -S

CPPFLAGS_vmlinux.lds = -DDRAM_VIRTUAL_BASE=0x$(CONFIG_ETRAX_DRAM_VIRTUAL_BASE)
AFLAGS += -mlinux

CFLAGS := $(CFLAGS) -mlinux -march=$(arch-y) -pipe

ifdef CONFIG_FRAME_POINTER
CFLAGS := $(subst -fomit-frame-pointer,,$(CFLAGS)) -g
CFLAGS += -fno-omit-frame-pointer
endif

head-y := arch/$(ARCH)/$(SARCH)/kernel/head.o

LIBGCC = $(shell $(CC) $(CFLAGS) -print-file-name=libgcc.a)

core-y		+= arch/$(ARCH)/kernel/ arch/$(ARCH)/mm/
core-y		+= arch/$(ARCH)/$(SARCH)/kernel/ arch/$(ARCH)/$(SARCH)/mm/
drivers-y	+= arch/$(ARCH)/$(SARCH)/drivers/
libs-y		+= arch/$(ARCH)/$(SARCH)/lib/ $(LIBGCC)

vmlinux.bin: vmlinux
	$(OBJCOPY) $(OBJCOPYFLAGS) vmlinux vmlinux.bin

timage: vmlinux.bin
	cat vmlinux.bin cramfs.img >timage

simimage: timage
	cp vmlinux.bin simvmlinux.bin

# the following will remake timage without compiling the kernel
# it does of course require that all object files exist...

cramfs:
## cramfs      - Creates a cramfs image
	mkcramfs -b 8192 -m romfs_meta.txt root cramfs.img
	cat vmlinux.bin cramfs.img >timage

clinux: vmlinux.bin decompress.bin rescue.bin

decompress.bin: FORCE
	@make -C arch/$(ARCH)/boot/compressed decompress.bin

rescue.bin: FORCE
	@make -C arch/$(ARCH)/boot/rescue rescue.bin

zImage: vmlinux.bin rescue.bin
## zImage     - Compressed kernel (gzip)
	@make -C arch/$(ARCH)/boot/ zImage

compressed: zImage

archmrproper:
archclean:
	$(Q)$(MAKE) $(clean)=arch/$(ARCH)/boot
	rm -f timage vmlinux.bin decompress.bin rescue.bin cramfs.img
	rm -rf $(LD_SCRIPT).tmp

prepare: arch/$(ARCH)/.links include/asm-$(ARCH)/.arch \
	 include/asm-$(ARCH)/$(SARCH)/offset.h

# Create some links to make all tools happy
arch/$(ARCH)/.links:
	@rm -rf arch/$(ARCH)/drivers
	@ln -sfn $(SARCH)/drivers arch/$(ARCH)/drivers
	@rm -rf arch/$(ARCH)/boot
	@ln -sfn $(SARCH)/boot arch/$(ARCH)/boot
	@rm -rf arch/$(ARCH)/lib
	@ln -sfn $(SARCH)/lib arch/$(ARCH)/lib
	@ln -sfn $(SARCH) arch/$(ARCH)/arch
	@ln -sfn ../$(SARCH)/vmlinux.lds.S arch/$(ARCH)/kernel/vmlinux.lds.S
	@touch $@

# Create link to sub arch includes
include/asm-$(ARCH)/.arch: $(wildcard include/config/arch/*.h)
	@echo '  Making asm-$(ARCH)/arch -> asm-$(ARCH)/$(SARCH) symlink'
	@rm -f include/asm-$(ARCH)/arch
	@ln -sf $(SARCH) include/asm-$(ARCH)/arch
	@touch $@

arch/$(ARCH)/$(SARCH)/kernel/asm-offsets.s: include/asm include/linux/version.h \
					include/config/MARKER

include/asm-$(ARCH)/$(SARCH)/offset.h: arch/$(ARCH)/$(SARCH)/kernel/asm-offsets.s
	$(call filechk,gen-asm-offsets)
