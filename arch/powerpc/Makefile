# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
# Changes for PPC by Gary Thomas
# Rewritten by Cort Dougan and Paul Mackerras
#

ifdef cross_compiling
  ifeq ($(CROSS_COMPILE),)
    # Auto detect cross compiler prefix.
    # Look for: (powerpc(64(le)?)?)(-unknown)?-linux(-gnu)?-
    CC_ARCHES := powerpc powerpc64 powerpc64le
    CC_SUFFIXES := linux linux-gnu unknown-linux-gnu
    CROSS_COMPILE := $(call cc-cross-prefix, $(foreach a,$(CC_ARCHES), \
                       $(foreach s,$(CC_SUFFIXES),$(a)-$(s)-)))
  endif
endif

HAS_BIARCH	:= $(call cc-option-yn, -m32)

# Set default 32 bits cross compilers for vdso and boot wrapper
CROSS32_COMPILE ?=

# If we're on a ppc/ppc64/ppc64le machine use that defconfig, otherwise just use
# ppc64le_defconfig because we have nothing better to go on.
uname := $(shell uname -m)
KBUILD_DEFCONFIG := $(if $(filter ppc%,$(uname)),$(uname),ppc64le)_defconfig

new_nm := $(shell if $(NM) --help 2>&1 | grep -- '--synthetic' > /dev/null; then echo y; else echo n; fi)

ifeq ($(new_nm),y)
NM		:= $(NM) --synthetic
endif

# BITS is used as extension for files which are available in a 32 bit
# and a 64 bit version to simplify shared Makefiles.
# e.g.: obj-y += foo_$(BITS).o
export BITS

ifdef CONFIG_PPC64
        BITS := 64
else
        BITS := 32
endif

machine-y = ppc
machine-$(CONFIG_PPC64) += 64
machine-$(CONFIG_CPU_LITTLE_ENDIAN) += le
UTS_MACHINE := $(subst $(space),,$(machine-y))

ifeq ($(CONFIG_PPC64)$(CONFIG_LD_IS_BFD),yy)
# Have the linker provide sfpr if possible.
# There is a corresponding test in arch/powerpc/lib/Makefile
KBUILD_LDFLAGS_MODULE += --save-restore-funcs
else
KBUILD_LDFLAGS_MODULE += arch/powerpc/lib/crtsavres.o
endif

ifdef CONFIG_CPU_LITTLE_ENDIAN
KBUILD_CPPFLAGS	+= -mlittle-endian
KBUILD_LDFLAGS	+= -EL
LDEMULATION	:= lppc
GNUTARGET	:= powerpcle
MULTIPLEWORD	:= -mno-multiple
KBUILD_CFLAGS_MODULE += $(call cc-option,-mno-save-toc-indirect)
else
KBUILD_CPPFLAGS	+= $(call cc-option,-mbig-endian)
KBUILD_LDFLAGS	+= -EB
LDEMULATION	:= ppc
GNUTARGET	:= powerpc
MULTIPLEWORD	:= -mmultiple
endif

ifdef CONFIG_PPC64
ifndef CONFIG_CC_IS_CLANG
cflags-$(CONFIG_PPC64_ELF_ABI_V1)	+= $(call cc-option,-mabi=elfv1)
cflags-$(CONFIG_PPC64_ELF_ABI_V1)	+= $(call cc-option,-mcall-aixdesc)
aflags-$(CONFIG_PPC64_ELF_ABI_V1)	+= $(call cc-option,-mabi=elfv1)
aflags-$(CONFIG_PPC64_ELF_ABI_V2)	+= -mabi=elfv2
endif
endif

ifndef CONFIG_CC_IS_CLANG
  cflags-$(CONFIG_CPU_LITTLE_ENDIAN)	+= -mno-strict-align
endif

cflags-$(CONFIG_CPU_BIG_ENDIAN)		+= $(call cc-option,-mbig-endian)
cflags-$(CONFIG_CPU_LITTLE_ENDIAN)	+= -mlittle-endian
aflags-$(CONFIG_CPU_BIG_ENDIAN)		+= $(call cc-option,-mbig-endian)
aflags-$(CONFIG_CPU_LITTLE_ENDIAN)	+= -mlittle-endian

ifeq ($(HAS_BIARCH),y)
KBUILD_CPPFLAGS	+= -m$(BITS)
KBUILD_AFLAGS	+= -m$(BITS)
KBUILD_LDFLAGS	+= -m elf$(BITS)$(LDEMULATION)
endif

LDFLAGS_vmlinux-y := -Bstatic
LDFLAGS_vmlinux-$(CONFIG_RELOCATABLE) := -pie
LDFLAGS_vmlinux-$(CONFIG_RELOCATABLE) += -z notext
LDFLAGS_vmlinux	:= $(LDFLAGS_vmlinux-y)

ifdef CONFIG_PPC64
ifndef CONFIG_PPC_KERNEL_PCREL
	# -mcmodel=medium breaks modules because it uses 32bit offsets from
	# the TOC pointer to create pointers where possible. Pointers into the
	# percpu data area are created by this method.
	#
	# The kernel module loader relocates the percpu data section from the
	# original location (starting with 0xd...) to somewhere in the base
	# kernel percpu data space (starting with 0xc...). We need a full
	# 64bit relocation for this to work, hence -mcmodel=large.
	KBUILD_CFLAGS_MODULE += -mcmodel=large
endif
endif

CFLAGS-$(CONFIG_PPC64)	:= $(call cc-option,-mtraceback=no)
ifdef CONFIG_PPC64_ELF_ABI_V2
CFLAGS-$(CONFIG_PPC64)	+= $(call cc-option,-mabi=elfv2,$(call cc-option,-mcall-aixdesc))
else
ifndef CONFIG_CC_IS_CLANG
CFLAGS-$(CONFIG_PPC64)	+= $(call cc-option,-mabi=elfv1)
CFLAGS-$(CONFIG_PPC64)	+= $(call cc-option,-mcall-aixdesc)
endif
endif
CFLAGS-$(CONFIG_PPC64)	+= -mcmodel=medium
CFLAGS-$(CONFIG_PPC64)	+= $(call cc-option,-mno-pointers-to-nested-functions)
CFLAGS-$(CONFIG_PPC64)	+= $(call cc-option,-mlong-double-128)

# Clang unconditionally reserves r2 on ppc32 and does not support the flag
# https://llvm.org/pr39555
CFLAGS-$(CONFIG_PPC32)	:= $(call cc-option, -ffixed-r2)

# Clang doesn't support -mmultiple / -mno-multiple
# https://llvm.org/pr39556
CFLAGS-$(CONFIG_PPC32)	+= $(call cc-option, $(MULTIPLEWORD))

CFLAGS-$(CONFIG_PPC32)	+= $(call cc-option,-mno-readonly-in-sdata)

CC_FLAGS_FPU		:= $(call cc-option,-mhard-float)
CC_FLAGS_NO_FPU		:= $(call cc-option,-msoft-float)

ifdef CONFIG_FUNCTION_TRACER
ifdef CONFIG_ARCH_USING_PATCHABLE_FUNCTION_ENTRY
KBUILD_CPPFLAGS	+= -DCC_USING_PATCHABLE_FUNCTION_ENTRY
ifdef CONFIG_PPC_FTRACE_OUT_OF_LINE
CC_FLAGS_FTRACE := -fpatchable-function-entry=1
else
ifdef CONFIG_DYNAMIC_FTRACE_WITH_CALL_OPS # PPC32 only
CC_FLAGS_FTRACE := -fpatchable-function-entry=3,1
else
CC_FLAGS_FTRACE := -fpatchable-function-entry=2
endif
endif
else
CC_FLAGS_FTRACE := -pg
ifdef CONFIG_MPROFILE_KERNEL
CC_FLAGS_FTRACE += -mprofile-kernel
endif
endif
endif

CFLAGS-$(CONFIG_TARGET_CPU_BOOL) += -mcpu=$(CONFIG_TARGET_CPU)
AFLAGS-$(CONFIG_TARGET_CPU_BOOL) += -mcpu=$(CONFIG_TARGET_CPU)

CFLAGS-y += $(CONFIG_TUNE_CPU)

asinstr := $(call as-instr,lis 9$(comma)foo@high,-DHAVE_AS_ATHIGH=1)

KBUILD_CPPFLAGS	+= -I $(srctree)/arch/powerpc $(asinstr)
KBUILD_AFLAGS	+= $(AFLAGS-y)
KBUILD_CFLAGS	+= $(CC_FLAGS_NO_FPU)
KBUILD_CFLAGS	+= $(CFLAGS-y)

CHECKFLAGS	+= -m$(BITS) -D__powerpc__ -D__powerpc$(BITS)__
ifdef CONFIG_CPU_BIG_ENDIAN
CHECKFLAGS	+= -D__BIG_ENDIAN__
else
CHECKFLAGS	+= -D__LITTLE_ENDIAN__
endif

ifdef CONFIG_476FPE_ERR46
	KBUILD_LDFLAGS_MODULE += --ppc476-workaround \
		-T $(srctree)/arch/powerpc/platforms/44x/ppc476_modules.lds
endif

# No prefix or pcrel
ifdef CONFIG_PPC_KERNEL_PREFIXED
KBUILD_CFLAGS += $(call cc-option,-mprefixed)
else
KBUILD_CFLAGS += $(call cc-option,-mno-prefixed)
endif
ifdef CONFIG_PPC_KERNEL_PCREL
KBUILD_CFLAGS += $(call cc-option,-mpcrel)
else
KBUILD_CFLAGS += $(call cc-option,-mno-pcrel)
endif

# No AltiVec or VSX or MMA instructions when building kernel
KBUILD_CFLAGS += $(call cc-option,-mno-altivec)
KBUILD_CFLAGS += $(call cc-option,-mno-vsx)
KBUILD_CFLAGS += $(call cc-option,-mno-mma)

# No SPE instruction when building kernel
# (We use all available options to help semi-broken compilers)
KBUILD_CFLAGS += $(call cc-option,-mno-spe)
KBUILD_CFLAGS += $(call cc-option,-mspe=no)

# Don't emit .eh_frame since we have no use for it
KBUILD_CFLAGS += -fno-asynchronous-unwind-tables

# Never use string load/store instructions as they are
# often slow when they are implemented at all
KBUILD_CFLAGS		+= $(call cc-option,-mno-string)

KBUILD_AFLAGS += $(aflags-y)
KBUILD_CFLAGS += $(cflags-y)

# Default to zImage, override when needed
all: zImage

# With make 3.82 we cannot mix normal and wildcard targets
BOOT_TARGETS1 := zImage zImage.initrd uImage
BOOT_TARGETS2 := zImage% dtbImage% treeImage.% cuImage.% simpleImage.% uImage.%

PHONY += $(BOOT_TARGETS1) $(BOOT_TARGETS2)

boot := arch/powerpc/boot

$(BOOT_TARGETS1): vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(patsubst %,$(boot)/%,$@)
$(BOOT_TARGETS2): vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(patsubst %,$(boot)/%,$@)


PHONY += bootwrapper_install
bootwrapper_install:
	$(Q)$(MAKE) $(build)=$(boot) $(patsubst %,$(boot)/%,$@)

include $(srctree)/scripts/Makefile.defconf

generated_configs += ppc64le_defconfig
ppc64le_defconfig:
	$(call merge_into_defconfig,ppc64_defconfig,le)

generated_configs += ppc64le_guest_defconfig
ppc64le_guest_defconfig:
	$(call merge_into_defconfig,ppc64_defconfig,le guest kvm_guest)

generated_configs += ppc64_guest_defconfig
ppc64_guest_defconfig:
	$(call merge_into_defconfig,ppc64_defconfig,be guest kvm_guest)

generated_configs += pseries_le_defconfig
pseries_le_defconfig: ppc64le_guest_defconfig

generated_configs += pseries_defconfig
pseries_defconfig: ppc64le_guest_defconfig

generated_configs += powernv_be_defconfig
powernv_be_defconfig:
	$(call merge_into_defconfig,powernv_defconfig,be)

generated_configs += mpc85xx_defconfig
mpc85xx_defconfig:
	$(call merge_into_defconfig,mpc85xx_base.config,\
		85xx-32bit 85xx-hw fsl-emb-nonhw)

generated_configs += mpc85xx_smp_defconfig
mpc85xx_smp_defconfig:
	$(call merge_into_defconfig,mpc85xx_base.config,\
		85xx-32bit 85xx-smp 85xx-hw fsl-emb-nonhw)

generated_configs += corenet32_smp_defconfig
corenet32_smp_defconfig:
	$(call merge_into_defconfig,corenet_base.config,\
		85xx-32bit 85xx-smp 85xx-hw fsl-emb-nonhw dpaa)

generated_configs += corenet64_smp_defconfig
corenet64_smp_defconfig:
	$(call merge_into_defconfig,corenet_base.config,\
		85xx-64bit 85xx-smp altivec 85xx-hw fsl-emb-nonhw dpaa)

generated_configs += mpc86xx_defconfig
mpc86xx_defconfig:
	$(call merge_into_defconfig,mpc86xx_base.config,\
		86xx-hw fsl-emb-nonhw)

generated_configs += mpc86xx_smp_defconfig
mpc86xx_smp_defconfig:
	$(call merge_into_defconfig,mpc86xx_base.config,\
		86xx-smp 86xx-hw fsl-emb-nonhw)

generated_configs += ppc32_allmodconfig
ppc32_allmodconfig:
	$(Q)$(MAKE) KCONFIG_ALLCONFIG=$(srctree)/arch/powerpc/configs/book3s_32.config \
		-f $(srctree)/Makefile allmodconfig

generated_configs += ppc44x_allmodconfig
ppc44x_allmodconfig:
	$(Q)$(MAKE) KCONFIG_ALLCONFIG=$(srctree)/arch/powerpc/configs/44x.config \
		-f $(srctree)/Makefile allmodconfig

generated_configs += ppc8xx_allmodconfig
ppc8xx_allmodconfig:
	$(Q)$(MAKE) KCONFIG_ALLCONFIG=$(srctree)/arch/powerpc/configs/8xx.config \
		-f $(srctree)/Makefile allmodconfig

generated_configs += ppc85xx_allmodconfig
ppc85xx_allmodconfig:
	$(Q)$(MAKE) KCONFIG_ALLCONFIG=$(srctree)/arch/powerpc/configs/85xx-32bit.config \
		-f $(srctree)/Makefile allmodconfig

generated_configs += ppc_defconfig
ppc_defconfig:
	$(call merge_into_defconfig,book3s_32.config,)

generated_configs += ppc64le_allmodconfig
ppc64le_allmodconfig:
	$(Q)$(MAKE) KCONFIG_ALLCONFIG=$(srctree)/arch/powerpc/configs/le.config \
		-f $(srctree)/Makefile allmodconfig

generated_configs += ppc64le_allnoconfig
ppc64le_allnoconfig:
	$(Q)$(MAKE) KCONFIG_ALLCONFIG=$(srctree)/arch/powerpc/configs/ppc64le.config \
		-f $(srctree)/Makefile allnoconfig

generated_configs += ppc64_book3e_allmodconfig
ppc64_book3e_allmodconfig:
	$(Q)$(MAKE) KCONFIG_ALLCONFIG=$(srctree)/arch/powerpc/configs/85xx-64bit.config \
		-f $(srctree)/Makefile allmodconfig

generated_configs += ppc32_randconfig
ppc32_randconfig:
	$(Q)$(MAKE) KCONFIG_ALLCONFIG=$(srctree)/arch/powerpc/configs/32-bit.config \
		-f $(srctree)/Makefile randconfig

generated_configs += ppc64_randconfig
ppc64_randconfig:
	$(Q)$(MAKE) KCONFIG_ALLCONFIG=$(srctree)/arch/powerpc/configs/64-bit.config \
		-f $(srctree)/Makefile randconfig

PHONY += $(generated_configs)

define archhelp
  echo '* zImage          - Build default images selected by kernel config'
  echo '  zImage.*        - Compressed kernel image (arch/powerpc/boot/zImage.*)'
  echo '  uImage          - U-Boot native image format'
  echo '  cuImage.<dt>    - Backwards compatible U-Boot image for older'
  echo '                    versions which do not support device trees'
  echo '  dtbImage.<dt>   - zImage with an embedded device tree blob'
  echo '  simpleImage.<dt> - Firmware independent image.'
  echo '  treeImage.<dt>  - Support for older IBM 4xx firmware (not U-Boot)'
  echo '  install         - Install kernel using'
  echo '                    (your) ~/bin/$(INSTALLKERNEL) or'
  echo '                    (distribution) /sbin/$(INSTALLKERNEL) or'
  echo '                    install to $$(INSTALL_PATH)'
  echo '  *_defconfig     - Select default config from arch/powerpc/configs'
  echo ''
  echo '  Targets with <dt> embed a device tree blob inside the image'
  echo '  These targets support board with firmware that does not'
  echo '  support passing a device tree directly.  Replace <dt> with the'
  echo '  name of a dts file from the arch/powerpc/boot/dts/ directory'
  echo '  (minus the .dts extension).'
  echo
  $(foreach cfg,$(generated_configs),
    printf "  %-27s - Build for %s\\n" $(cfg) $(subst _defconfig,,$(cfg));)
endef

PHONY += install
install:
	$(call cmd,install)

ifeq ($(KBUILD_EXTMOD),)
# We need to generate vdso-offsets.h before compiling certain files in kernel/.
# In order to do that, we should use the archprepare target, but we can't since
# asm-offsets.h is included in some files used to generate vdso-offsets.h, and
# asm-offsets.h is built in prepare0, for which archprepare is a dependency.
# Therefore we need to generate the header after prepare0 has been made, hence
# this hack.
prepare: vdso_prepare
vdso_prepare: prepare0
	$(if $(CONFIG_VDSO32),$(Q)$(MAKE) \
		$(build)=arch/powerpc/kernel/vdso include/generated/vdso32-offsets.h)
	$(if $(CONFIG_PPC64),$(Q)$(MAKE) \
		$(build)=arch/powerpc/kernel/vdso include/generated/vdso64-offsets.h)
endif

archprepare: checkbin

archheaders:
	$(Q)$(MAKE) $(build)=arch/powerpc/kernel/syscalls all

ifdef CONFIG_STACKPROTECTOR
prepare: stack_protector_prepare

PHONY += stack_protector_prepare
stack_protector_prepare: prepare0
ifdef CONFIG_PPC64
	$(eval KBUILD_CFLAGS += -mstack-protector-guard=tls -mstack-protector-guard-reg=r13 \
				-mstack-protector-guard-offset=$(shell awk '{if ($$2 == "PACA_CANARY") print $$3;}' \
				$(objtree)/include/generated/asm-offsets.h))
else
	$(eval KBUILD_CFLAGS += -mstack-protector-guard=tls -mstack-protector-guard-reg=r2 \
				-mstack-protector-guard-offset=$(shell awk '{if ($$2 == "TASK_CANARY") print $$3;}' \
				$(objtree)/include/generated/asm-offsets.h))
endif
endif

PHONY += checkbin
checkbin:
	@if test "x${CONFIG_FTRACE_MCOUNT_USE_RECORDMCOUNT}" = "xy" -a \
		"x${CONFIG_LD_IS_BFD}" = "xy" -a \
		"${CONFIG_LD_VERSION}" = "23700" ; then \
		echo -n '*** binutils 2.37 drops unused section symbols, which recordmcount ' ; \
		echo 'is unable to handle.' ; \
		echo '*** Please use a different binutils version.' ; \
		false ; \
	fi
