# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

OBJCOPYFLAGS    := -O binary
LDFLAGS_vmlinux :=
ifeq ($(CONFIG_DYNAMIC_FTRACE),y)
	LDFLAGS_vmlinux := --no-relax
	KBUILD_CPPFLAGS += -DCC_USING_PATCHABLE_FUNCTION_ENTRY
	CC_FLAGS_FTRACE := -fpatchable-function-entry=8
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
else
	BITS := 32
	UTS_MACHINE := riscv32

	KBUILD_CFLAGS += -mabi=ilp32
	KBUILD_AFLAGS += -mabi=ilp32
	KBUILD_LDFLAGS += -melf32lriscv
endif

ifeq ($(CONFIG_LD_IS_LLD),y)
	KBUILD_CFLAGS += -mno-relax
	KBUILD_AFLAGS += -mno-relax
ifndef CONFIG_AS_IS_LLVM
	KBUILD_CFLAGS += -Wa,-mno-relax
	KBUILD_AFLAGS += -Wa,-mno-relax
endif
endif

# ISA string setting
riscv-march-$(CONFIG_ARCH_RV32I)	:= rv32ima
riscv-march-$(CONFIG_ARCH_RV64I)	:= rv64ima
riscv-march-$(CONFIG_FPU)		:= $(riscv-march-y)fd
riscv-march-$(CONFIG_RISCV_ISA_C)	:= $(riscv-march-y)c

# Newer binutils versions default to ISA spec version 20191213 which moves some
# instructions from the I extension to the Zicsr and Zifencei extensions.
toolchain-need-zicsr-zifencei := $(call cc-option-yn, -march=$(riscv-march-y)_zicsr_zifencei)
riscv-march-$(toolchain-need-zicsr-zifencei) := $(riscv-march-y)_zicsr_zifencei

KBUILD_CFLAGS += -march=$(subst fd,,$(riscv-march-y))
KBUILD_AFLAGS += -march=$(riscv-march-y)

KBUILD_CFLAGS += -mno-save-restore
KBUILD_CFLAGS += -DCONFIG_PAGE_OFFSET=$(CONFIG_PAGE_OFFSET)

ifeq ($(CONFIG_CMODEL_MEDLOW),y)
	KBUILD_CFLAGS += -mcmodel=medlow
endif
ifeq ($(CONFIG_CMODEL_MEDANY),y)
	KBUILD_CFLAGS += -mcmodel=medany
endif
ifeq ($(CONFIG_PERF_EVENTS),y)
        KBUILD_CFLAGS += -fno-omit-frame-pointer
endif

KBUILD_CFLAGS_MODULE += $(call cc-option,-mno-relax)

# GCC versions that support the "-mstrict-align" option default to allowing
# unaligned accesses.  While unaligned accesses are explicitly allowed in the
# RISC-V ISA, they're emulated by machine mode traps on all extant
# architectures.  It's faster to have GCC emit only aligned accesses.
KBUILD_CFLAGS += $(call cc-option,-mstrict-align)

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
KBUILD_IMAGE	:= $(boot)/Image.gz
endif

head-y := arch/riscv/kernel/head.o

core-$(CONFIG_RISCV_ERRATA_ALTERNATIVE) += arch/riscv/errata/
core-$(CONFIG_KVM) += arch/riscv/kvm/

libs-y += arch/riscv/lib/
libs-$(CONFIG_EFI_STUB) += $(objtree)/drivers/firmware/efi/libstub/lib.a

PHONY += vdso_install
vdso_install:
	$(Q)$(MAKE) $(build)=arch/riscv/kernel/vdso $@

ifeq ($(KBUILD_EXTMOD),)
ifeq ($(CONFIG_MMU),y)
prepare: vdso_prepare
vdso_prepare: prepare0
	$(Q)$(MAKE) $(build)=arch/riscv/kernel/vdso include/generated/vdso-offsets.h
endif
endif

ifneq ($(CONFIG_XIP_KERNEL),y)
ifeq ($(CONFIG_RISCV_M_MODE)$(CONFIG_SOC_CANAAN),yy)
KBUILD_IMAGE := $(boot)/loader.bin
else
KBUILD_IMAGE := $(boot)/Image.gz
endif
endif
BOOT_TARGETS := Image Image.gz loader loader.bin xipImage

all:	$(notdir $(KBUILD_IMAGE))

$(BOOT_TARGETS): vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@
	@$(kecho) '  Kernel: $(boot)/$@ is ready'

Image.%: Image
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

install: install-image = Image
zinstall: install-image = Image.gz
install zinstall:
	$(CONFIG_SHELL) $(srctree)/$(boot)/install.sh $(KERNELRELEASE) \
	$(boot)/$(install-image) System.map "$(INSTALL_PATH)"

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
