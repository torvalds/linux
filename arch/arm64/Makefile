#
# arch/arm64/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995-2001 by Russell King

LDFLAGS_vmlinux	:=--no-undefined -X

ifeq ($(CONFIG_RELOCATABLE), y)
# Pass --no-apply-dynamic-relocs to restore pre-binutils-2.27 behaviour
# for relative relocs, since this leads to better Image compression
# with the relocation offsets always being zero.
LDFLAGS_vmlinux		+= -shared -Bsymbolic -z notext \
			$(call ld-option, --no-apply-dynamic-relocs)
endif

ifeq ($(CONFIG_ARM64_ERRATUM_843419),y)
  ifeq ($(CONFIG_ARM64_LD_HAS_FIX_ERRATUM_843419),y)
LDFLAGS_vmlinux	+= --fix-cortex-a53-843419
  endif
endif

cc_has_k_constraint := $(call try-run,echo				\
	'int main(void) {						\
		asm volatile("and w0, w0, %w0" :: "K" (4294967295));	\
		return 0;						\
	}' | $(CC) -S -x c -o "$$TMP" -,,-DCONFIG_CC_HAS_K_CONSTRAINT=1)

ifeq ($(CONFIG_BROKEN_GAS_INST),y)
$(warning Detected assembler with broken .inst; disassembly will be unreliable)
endif

KBUILD_CFLAGS	+= -mgeneral-regs-only	\
		   $(compat_vdso) $(cc_has_k_constraint)
KBUILD_CFLAGS	+= $(call cc-disable-warning, psabi)
KBUILD_AFLAGS	+= $(compat_vdso)

KBUILD_CFLAGS	+= $(call cc-option,-mabi=lp64)
KBUILD_AFLAGS	+= $(call cc-option,-mabi=lp64)

# Avoid generating .eh_frame* sections.
KBUILD_CFLAGS	+= -fno-asynchronous-unwind-tables -fno-unwind-tables
KBUILD_AFLAGS	+= -fno-asynchronous-unwind-tables -fno-unwind-tables

ifeq ($(CONFIG_STACKPROTECTOR_PER_TASK),y)
prepare: stack_protector_prepare
stack_protector_prepare: prepare0
	$(eval KBUILD_CFLAGS += -mstack-protector-guard=sysreg		  \
				-mstack-protector-guard-reg=sp_el0	  \
				-mstack-protector-guard-offset=$(shell	  \
			awk '{if ($$2 == "TSK_STACK_CANARY") print $$3;}' \
					include/generated/asm-offsets.h))
endif

ifeq ($(CONFIG_AS_HAS_ARMV8_2), y)
# make sure to pass the newest target architecture to -march.
asm-arch := armv8.2-a
endif

# Ensure that if the compiler supports branch protection we default it
# off, this will be overridden if we are using branch protection.
branch-prot-flags-y += $(call cc-option,-mbranch-protection=none)

ifeq ($(CONFIG_ARM64_PTR_AUTH_KERNEL),y)
branch-prot-flags-$(CONFIG_CC_HAS_SIGN_RETURN_ADDRESS) := -msign-return-address=all
# We enable additional protection for leaf functions as there is some
# narrow potential for ROP protection benefits and no substantial
# performance impact has been observed.
ifeq ($(CONFIG_ARM64_BTI_KERNEL),y)
branch-prot-flags-$(CONFIG_CC_HAS_BRANCH_PROT_PAC_RET_BTI) := -mbranch-protection=pac-ret+leaf+bti
else
branch-prot-flags-$(CONFIG_CC_HAS_BRANCH_PROT_PAC_RET) := -mbranch-protection=pac-ret+leaf
endif
# -march=armv8.3-a enables the non-nops instructions for PAC, to avoid the
# compiler to generate them and consequently to break the single image contract
# we pass it only to the assembler. This option is utilized only in case of non
# integrated assemblers.
ifeq ($(CONFIG_AS_HAS_PAC), y)
asm-arch := armv8.3-a
endif
endif

KBUILD_CFLAGS += $(branch-prot-flags-y)

ifeq ($(CONFIG_AS_HAS_ARMV8_4), y)
# make sure to pass the newest target architecture to -march.
asm-arch := armv8.4-a
endif

ifeq ($(CONFIG_AS_HAS_ARMV8_5), y)
# make sure to pass the newest target architecture to -march.
asm-arch := armv8.5-a
endif

ifdef asm-arch
KBUILD_CFLAGS	+= -Wa,-march=$(asm-arch) \
		   -DARM64_ASM_ARCH='"$(asm-arch)"'
endif

ifeq ($(CONFIG_SHADOW_CALL_STACK), y)
KBUILD_CFLAGS	+= -ffixed-x18
endif

ifeq ($(CONFIG_CPU_BIG_ENDIAN), y)
KBUILD_CPPFLAGS	+= -mbig-endian
CHECKFLAGS	+= -D__AARCH64EB__
# Prefer the baremetal ELF build target, but not all toolchains include
# it so fall back to the standard linux version if needed.
KBUILD_LDFLAGS	+= -EB $(call ld-option, -maarch64elfb, -maarch64linuxb -z norelro)
UTS_MACHINE	:= aarch64_be
else
KBUILD_CPPFLAGS	+= -mlittle-endian
CHECKFLAGS	+= -D__AARCH64EL__
# Same as above, prefer ELF but fall back to linux target if needed.
KBUILD_LDFLAGS	+= -EL $(call ld-option, -maarch64elf, -maarch64linux -z norelro)
UTS_MACHINE	:= aarch64
endif

ifeq ($(CONFIG_LD_IS_LLD), y)
KBUILD_LDFLAGS	+= -z norelro
endif

CHECKFLAGS	+= -D__aarch64__

ifeq ($(CONFIG_DYNAMIC_FTRACE_WITH_REGS),y)
  KBUILD_CPPFLAGS += -DCC_USING_PATCHABLE_FUNCTION_ENTRY
  CC_FLAGS_FTRACE := -fpatchable-function-entry=2
endif

ifeq ($(CONFIG_KASAN_SW_TAGS), y)
KASAN_SHADOW_SCALE_SHIFT := 4
else ifeq ($(CONFIG_KASAN_GENERIC), y)
KASAN_SHADOW_SCALE_SHIFT := 3
endif

KBUILD_CFLAGS += -DKASAN_SHADOW_SCALE_SHIFT=$(KASAN_SHADOW_SCALE_SHIFT)
KBUILD_CPPFLAGS += -DKASAN_SHADOW_SCALE_SHIFT=$(KASAN_SHADOW_SCALE_SHIFT)
KBUILD_AFLAGS += -DKASAN_SHADOW_SCALE_SHIFT=$(KASAN_SHADOW_SCALE_SHIFT)

libs-y		:= arch/arm64/lib/ $(libs-y)
libs-$(CONFIG_EFI_STUB) += $(objtree)/drivers/firmware/efi/libstub/lib.a

# Default target when executing plain make
boot		:= arch/arm64/boot

ifeq ($(CONFIG_EFI_ZBOOT),)
KBUILD_IMAGE	:= $(boot)/Image.gz
else
KBUILD_IMAGE	:= $(boot)/vmlinuz.efi
endif

all:	$(notdir $(KBUILD_IMAGE))

vmlinuz.efi: Image
Image vmlinuz.efi: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

Image.%: Image
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

install: KBUILD_IMAGE := $(boot)/Image
install zinstall:
	$(call cmd,install)

PHONY += vdso_install
vdso_install:
	$(Q)$(MAKE) $(build)=arch/arm64/kernel/vdso $@
	$(if $(CONFIG_COMPAT_VDSO), \
		$(Q)$(MAKE) $(build)=arch/arm64/kernel/vdso32 $@)

archprepare:
	$(Q)$(MAKE) $(build)=arch/arm64/tools kapi
ifeq ($(CONFIG_ARM64_ERRATUM_843419),y)
  ifneq ($(CONFIG_ARM64_LD_HAS_FIX_ERRATUM_843419),y)
	@echo "warning: ld does not support --fix-cortex-a53-843419; kernel may be susceptible to erratum" >&2
  endif
endif
ifeq ($(CONFIG_ARM64_USE_LSE_ATOMICS),y)
  ifneq ($(CONFIG_ARM64_LSE_ATOMICS),y)
	@echo "warning: LSE atomics not supported by binutils" >&2
  endif
endif

ifeq ($(KBUILD_EXTMOD),)
# We need to generate vdso-offsets.h before compiling certain files in kernel/.
# In order to do that, we should use the archprepare target, but we can't since
# asm-offsets.h is included in some files used to generate vdso-offsets.h, and
# asm-offsets.h is built in prepare0, for which archprepare is a dependency.
# Therefore we need to generate the header after prepare0 has been made, hence
# this hack.
prepare: vdso_prepare
vdso_prepare: prepare0
	$(Q)$(MAKE) $(build)=arch/arm64/kernel/vdso \
	include/generated/vdso-offsets.h arch/arm64/kernel/vdso/vdso.so
ifdef CONFIG_COMPAT_VDSO
	$(Q)$(MAKE) $(build)=arch/arm64/kernel/vdso32 \
	include/generated/vdso32-offsets.h arch/arm64/kernel/vdso32/vdso.so
endif
endif

define archhelp
  echo  '* Image.gz      - Compressed kernel image (arch/$(ARCH)/boot/Image.gz)'
  echo  '  Image         - Uncompressed kernel image (arch/$(ARCH)/boot/Image)'
  echo  '  install       - Install uncompressed kernel'
  echo  '  zinstall      - Install compressed kernel'
  echo  '                  Install using (your) ~/bin/installkernel or'
  echo  '                  (distribution) /sbin/installkernel or'
  echo  '                  install to $$(INSTALL_PATH) and run lilo'
endef
