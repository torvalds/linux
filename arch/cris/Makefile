#
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

arch-y := v10
arch-$(CONFIG_ETRAX_ARCH_V10) := v10
arch-$(CONFIG_ETRAX_ARCH_V32) := v32

# No config available for make clean etc
mach-y := fs
mach-$(CONFIG_CRIS_MACH_ARTPEC3) := a3
mach-$(CONFIG_ETRAXFS) := fs

ifneq ($(arch-y),)
SARCH := arch-$(arch-y)
else
SARCH :=
endif

ifneq ($(mach-y),)
MACH := mach-$(mach-y)
else
MACH :=
endif

LD = $(CROSS_COMPILE)ld -mcrislinux

OBJCOPYFLAGS := -O binary -R .note -R .comment -S

CPPFLAGS_vmlinux.lds = -DDRAM_VIRTUAL_BASE=0x$(CONFIG_ETRAX_DRAM_VIRTUAL_BASE)

KBUILD_AFLAGS += -mlinux -march=$(arch-y) -Iinclude/asm/arch/mach -Iinclude/asm/arch

KBUILD_CFLAGS += -mlinux -march=$(arch-y) -pipe -Iinclude/asm/arch/mach -Iinclude/asm/arch

ifdef CONFIG_FRAME_POINTER
KBUILD_CFLAGS := $(subst -fomit-frame-pointer,,$(KBUILD_CFLAGS)) -g
KBUILD_CFLAGS += -fno-omit-frame-pointer
endif

head-y := arch/$(ARCH)/$(SARCH)/kernel/head.o

LIBGCC = $(shell $(CC) $(KBUILD_CFLAGS) -print-file-name=libgcc.a)

core-y		+= arch/$(ARCH)/kernel/ arch/$(ARCH)/mm/
core-y		+= arch/$(ARCH)/$(SARCH)/kernel/ arch/$(ARCH)/$(SARCH)/mm/
ifdef CONFIG_ETRAX_ARCH_V32
core-y		+= arch/$(ARCH)/$(SARCH)/$(MACH)/
endif
drivers-y	+= arch/$(ARCH)/$(SARCH)/drivers/
libs-y		+= arch/$(ARCH)/$(SARCH)/lib/ $(LIBGCC)

# cris source path
SRC_ARCH              = $(srctree)/arch/$(ARCH)
# cris object files path
OBJ_ARCH              = $(objtree)/arch/$(ARCH)

boot := arch/$(ARCH)/boot
MACHINE := arch/$(ARCH)/$(SARCH)

all: zImage

zImage Image: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) MACHINE=$(MACHINE) $(boot)/$@

archprepare: $(SRC_ARCH)/.links $(srctree)/include/asm-$(ARCH)/.arch FORCE

# Create some links to make all tools happy
$(SRC_ARCH)/.links:
	@rm -rf $(SRC_ARCH)/drivers
	@ln -sfn $(SARCH)/drivers $(SRC_ARCH)/drivers
	@rm -rf $(SRC_ARCH)/boot
	@ln -sfn $(SARCH)/boot $(SRC_ARCH)/boot
	@rm -rf $(SRC_ARCH)/lib
	@ln -sfn $(SARCH)/lib $(SRC_ARCH)/lib
	@rm -f $(SRC_ARCH)/arch/mach
	@rm -rf $(SRC_ARCH)/arch
	@ln -sfn $(SARCH) $(SRC_ARCH)/arch
ifdef CONFIG_ETRAX_ARCH_V32
	@ln -sfn ../$(SARCH)/$(MACH) $(SRC_ARCH)/arch/mach
endif
	@rm -rf $(SRC_ARCH)/kernel/vmlinux.lds.S
	@ln -sfn ../$(SARCH)/vmlinux.lds.S $(SRC_ARCH)/kernel/vmlinux.lds.S
	@rm -rf $(SRC_ARCH)/kernel/asm-offsets.c
	@ln -sfn ../$(SARCH)/kernel/asm-offsets.c $(SRC_ARCH)/kernel/asm-offsets.c
	@touch $@

# Create link to sub arch includes
$(srctree)/include/asm-$(ARCH)/.arch: $(wildcard include/config/arch/*.h)
	@echo '  SYMLINK include/asm-$(ARCH)/arch -> include/asm-$(ARCH)/$(SARCH)'
	@rm -f $(srctree)/include/asm-$(ARCH)/arch/mach
	@rm -f $(srctree)/include/asm-$(ARCH)/arch
	@ln -sf $(SARCH) $(srctree)/include/asm-$(ARCH)/arch
ifdef CONFIG_ETRAX_ARCH_V32
	@ln -sf $(MACH) $(srctree)/include/asm-$(ARCH)/arch/mach
endif
	@touch $@

archclean:
	$(Q)if [ -e arch/$(ARCH)/boot ]; then \
		$(MAKE) $(clean)=arch/$(ARCH)/boot; \
	fi

CLEAN_FILES += \
	$(MACHINE)/boot/zImage \
	$(MACHINE)/boot/compressed/decompress.bin \
	$(MACHINE)/boot/compressed/piggy.gz \
	$(MACHINE)/boot/rescue/rescue.bin \
	$(SRC_ARCH)/.links \
	$(srctree)/include/asm-$(ARCH)/.arch

MRPROPER_FILES += \
	$(SRC_ARCH)/drivers \
	$(SRC_ARCH)/boot \
	$(SRC_ARCH)/lib \
	$(SRC_ARCH)/arch \
	$(SRC_ARCH)/kernel/vmlinux.lds.S \
	$(SRC_ARCH)/kernel/asm-offsets.c

define archhelp
  echo  '* zImage        - Compressed kernel image (arch/$(ARCH)/boot/zImage)'
  echo  '* Image         - Uncompressed kernel image (arch/$(ARCH)/boot/Image)'
endef
