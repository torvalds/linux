# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

LDFLAGS_vmlinux := -z norelro
ifeq ($(CONFIG_RELOCATABLE),y)
	LDFLAGS_vmlinux += -shared -Bsymbolic -z notext --emit-relocs
	KBUILD_CFLAGS += -fPIE
endif
ifeq ($(CONFIG_DYNAMIC_FTRACE),y)
	LDFLAGS_vmlinux += --no-relax
	KBUILD_CPPFLAGS += -DCC_USING_PATCHABLE_FUNCTION_ENTRY
ifeq ($(CONFIG_RISCV_ISA_C),y)
	CC_FLAGS_FTRACE := -fpatchable-function-entry=4
else
	CC_FLAGS_FTRACE := -fpatchable-function-entry=2
endif
endif

ifeq ($(CONFIG_CMODEL_MEDLOW),y)
KBUILD_CFLAGS_MODULE += -mcmodel=medany
endif

export BITS
ifeq ($(CONFIG_ARCH_RV64I),y)
	BITS := 64
	UTS_MACHINE := riscv64

	KBUILD_CFLAGS += -mabi=lp64
	KBUILD_AFLAGS += -mabi=lp64

	KBUILD_LDFLAGS += -melf64lriscv

	KBUILD_RUSTFLAGS += -Ctarget-cpu=generic-rv64 --target=riscv64imac-unknown-none-elf \
			    -Cno-redzone
else
	BITS := 32
	UTS_MACHINE := riscv32

	KBUILD_CFLAGS += -mabi=ilp32
	KBUILD_AFLAGS += -mabi=ilp32
	KBUILD_LDFLAGS += -melf32lriscv
endif

ifndef CONFIG_RISCV_USE_LINKER_RELAXATION
	KBUILD_CFLAGS += -mno-relax
	KBUILD_AFLAGS += -mno-relax
ifndef CONFIG_AS_IS_LLVM
	KBUILD_CFLAGS += -Wa,-mno-relax
	KBUILD_AFLAGS += -Wa,-mno-relax
endif
# LLVM has an issue with target-features and LTO: https://github.com/llvm/llvm-project/issues/59350
# Ensure it is aware of linker relaxation with LTO, otherwise relocations may
# be incorrect: https://github.com/llvm/llvm-project/issues/65090
else ifeq ($(CONFIG_LTO_CLANG),y)
	KBUILD_LDFLAGS += -mllvm -mattr=+c -mllvm -mattr=+relax
endif

ifeq ($(CONFIG_SHADOW_CALL_STACK),y)
	KBUILD_LDFLAGS += --no-relax-gp
endif

# ISA string setting
riscv-march-$(CONFIG_ARCH_RV32I)	:= rv32ima
riscv-march-$(CONFIG_ARCH_RV64I)	:= rv64ima
riscv-march-$(CONFIG_FPU)		:= $(riscv-march-y)fd
riscv-march-$(CONFIG_RISCV_ISA_C)	:= $(riscv-march-y)c
riscv-march-$(CONFIG_RISCV_ISA_V)	:= $(riscv-march-y)v

ifneq ($(CONFIG_RISCV_ISA_C),y)
	KBUILD_RUSTFLAGS += -Ctarget-feature=-c
endif

ifdef CONFIG_TOOLCHAIN_NEEDS_OLD_ISA_SPEC
KBUILD_CFLAGS += -Wa,-misa-spec=2.2
KBUILD_AFLAGS += -Wa,-misa-spec=2.2
else
riscv-march-$(CONFIG_TOOLCHAIN_NEEDS_EXPLICIT_ZICSR_ZIFENCEI) := $(riscv-march-y)_zicsr_zifencei
endif

# Remove F,D,V from isa string for all. Keep extensions between "fd" and "v" by
# matching non-v and non-multi-letter extensions out with the filter ([^v_]*)
KBUILD_CFLAGS += -march=$(shell echo $(riscv-march-y) | sed -E 's/(rv32ima|rv64ima)fd([^v_]*)v?/\1\2/')

KBUILD_AFLAGS += -march=$(riscv-march-y)

# For C code built with floating-point support, exclude V but keep F and D.
CC_FLAGS_FPU  := -march=$(shell echo $(riscv-march-y) | sed -E 's/(rv32ima|rv64ima)([^v_]*)v?/\1\2/')

KBUILD_CFLAGS += -mno-save-restore
KBUILD_CFLAGS += -DCONFIG_PAGE_OFFSET=$(CONFIG_PAGE_OFFSET)

ifeq ($(CONFIG_CMODEL_MEDLOW),y)
	KBUILD_CFLAGS += -mcmodel=medlow
endif
ifeq ($(CONFIG_CMODEL_MEDANY),y)
	KBUILD_CFLAGS += -mcmodel=medany
endif

# Avoid generating .eh_frame sections.
KBUILD_CFLAGS += -fno-asynchronous-unwind-tables -fno-unwind-tables

# The RISC-V attributes frequently cause compatibility issues and provide no
# information, so just turn them off.
KBUILD_CFLAGS += $(call cc-option,-mno-riscv-attribute)
KBUILD_AFLAGS += $(call cc-option,-mno-riscv-attribute)
KBUILD_CFLAGS += $(call as-option,-Wa$(comma)-mno-arch-attr)
KBUILD_AFLAGS += $(call as-option,-Wa$(comma)-mno-arch-attr)

KBUILD_CFLAGS_MODULE += $(call cc-option,-mno-relax)
KBUILD_AFLAGS_MODULE += $(call as-option,-Wa$(comma)-mno-relax)

# GCC versions that support the "-mstrict-align" option default to allowing
# unaligned accesses.  While unaligned accesses are explicitly allowed in the
# RISC-V ISA, they're emulated by machine mode traps on all extant
# architectures.  It's faster to have GCC emit only aligned accesses.
ifneq ($(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS),y)
KBUILD_CFLAGS += $(call cc-option,-mstrict-align)
endif

ifeq ($(CONFIG_STACKPROTECTOR_PER_TASK),y)
prepare: stack_protector_prepare
stack_protector_prepare: prepare0
	$(eval KBUILD_CFLAGS += -mstack-protector-guard=tls		  \
				-mstack-protector-guard-reg=tp		  \
				-mstack-protector-guard-offset=$(shell	  \
			awk '{if ($$2 == "TSK_STACK_CANARY") print $$3;}' \
					include/generated/asm-offsets.h))
endif

# arch specific predefines for sparse
CHECKFLAGS += -D__riscv -D__riscv_xlen=$(BITS)

# Default target when executing plain make
boot		:= arch/riscv/boot
ifeq ($(CONFIG_XIP_KERNEL),y)
KBUILD_IMAGE := $(boot)/xipImage
else
ifeq ($(CONFIG_RISCV_M_MODE)$(CONFIG_SOC_CANAAN_K210),yy)
KBUILD_IMAGE := $(boot)/loader.bin
else
ifeq ($(CONFIG_EFI_ZBOOT),)
KBUILD_IMAGE	:= $(boot)/Image.gz
else
KBUILD_IMAGE := $(boot)/vmlinuz.efi
endif
endif
endif

boot					:= arch/riscv/boot
boot-image-y				:= Image
boot-image-$(CONFIG_KERNEL_BZIP2)	:= Image.bz2
boot-image-$(CONFIG_KERNEL_GZIP)	:= Image.gz
boot-image-$(CONFIG_KERNEL_LZ4)		:= Image.lz4
boot-image-$(CONFIG_KERNEL_LZMA)	:= Image.lzma
boot-image-$(CONFIG_KERNEL_LZO)		:= Image.lzo
boot-image-$(CONFIG_KERNEL_ZSTD)	:= Image.zst
ifdef CONFIG_RISCV_M_MODE
boot-image-$(CONFIG_ARCH_CANAAN)	:= loader.bin
endif
boot-image-$(CONFIG_EFI_ZBOOT)		:= vmlinuz.efi
boot-image-$(CONFIG_XIP_KERNEL)		:= xipImage
KBUILD_IMAGE				:= $(boot)/$(boot-image-y)

libs-y += arch/riscv/lib/
libs-$(CONFIG_EFI_STUB) += $(objtree)/drivers/firmware/efi/libstub/lib.a

ifeq ($(KBUILD_EXTMOD),)
ifeq ($(CONFIG_MMU),y)
prepare: vdso_prepare
vdso_prepare: prepare0
	$(Q)$(MAKE) $(build)=arch/riscv/kernel/vdso include/generated/vdso-offsets.h
	$(if $(CONFIG_COMPAT),$(Q)$(MAKE) \
		$(build)=arch/riscv/kernel/compat_vdso include/generated/compat_vdso-offsets.h)

endif
endif

vdso-install-y			+= arch/riscv/kernel/vdso/vdso.so.dbg
vdso-install-$(CONFIG_COMPAT)	+= arch/riscv/kernel/compat_vdso/compat_vdso.so.dbg

BOOT_TARGETS := Image Image.gz Image.bz2 Image.lz4 Image.lzma Image.lzo Image.zst loader loader.bin xipImage vmlinuz.efi

all:	$(notdir $(KBUILD_IMAGE))

loader.bin: loader
Image.gz Image.bz2 Image.lz4 Image.lzma Image.lzo Image.zst loader xipImage vmlinuz.efi: Image

$(BOOT_TARGETS): vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@
	@$(kecho) '  Kernel: $(boot)/$@ is ready'

# the install target always installs KBUILD_IMAGE (which may be compressed)
# but keep the zinstall target for compatibility with older releases
install zinstall:
	$(call cmd,install)

PHONY += rv32_randconfig
rv32_randconfig:
	$(Q)$(MAKE) KCONFIG_ALLCONFIG=$(srctree)/arch/riscv/configs/32-bit.config \
		-f $(srctree)/Makefile randconfig

PHONY += rv64_randconfig
rv64_randconfig:
	$(Q)$(MAKE) KCONFIG_ALLCONFIG=$(srctree)/arch/riscv/configs/64-bit.config \
		-f $(srctree)/Makefile randconfig

PHONY += rv32_defconfig
rv32_defconfig:
	$(Q)$(MAKE) -f $(srctree)/Makefile defconfig 32-bit.config

PHONY += rv32_nommu_virt_defconfig
rv32_nommu_virt_defconfig:
	$(Q)$(MAKE) -f $(srctree)/Makefile nommu_virt_defconfig 32-bit.config

define archhelp
  echo  '  Image		- Uncompressed kernel image (arch/riscv/boot/Image)'
  echo  '  Image.gz	- Compressed kernel image (arch/riscv/boot/Image.gz)'
  echo  '  Image.bz2	- Compressed kernel image (arch/riscv/boot/Image.bz2)'
  echo  '  Image.lz4	- Compressed kernel image (arch/riscv/boot/Image.lz4)'
  echo  '  Image.lzma	- Compressed kernel image (arch/riscv/boot/Image.lzma)'
  echo  '  Image.lzo	- Compressed kernel image (arch/riscv/boot/Image.lzo)'
  echo  '  Image.zst	- Compressed kernel image (arch/riscv/boot/Image.zst)'
  echo  '  vmlinuz.efi	- Compressed EFI kernel image (arch/riscv/boot/vmlinuz.efi)'
  echo  '		  Default when CONFIG_EFI_ZBOOT=y'
  echo  '  xipImage	- Execute-in-place kernel image (arch/riscv/boot/xipImage)'
  echo  '		  Default when CONFIG_XIP_KERNEL=y'
  echo  '  install	- Install kernel using (your) ~/bin/$(INSTALLKERNEL) or'
  echo  '		  (distribution) /sbin/$(INSTALLKERNEL) or install to '
  echo  '		  $$(INSTALL_PATH)'
endef
