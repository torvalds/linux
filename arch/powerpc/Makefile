# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
# Changes for PPC by Gary Thomas
# Rewritten by Cort Dougan and Paul Mackerras
#

HAS_BIARCH	:= $(call cc-option-yn, -m32)

# Set default 32 bits cross compilers for vdso and boot wrapper
CROSS32_COMPILE ?=

ifeq ($(HAS_BIARCH),y)
ifeq ($(CROSS32_COMPILE),)
ifdef CONFIG_PPC32
# These options will be overridden by any -mcpu option that the CPU
# or platform code sets later on the command line, but they are needed
# to set a sane 32-bit cpu target for the 64-bit cross compiler which
# may default to the wrong ISA.
KBUILD_CFLAGS		+= -mcpu=powerpc
KBUILD_AFLAGS		+= -mcpu=powerpc
endif
endif
endif

ifdef CONFIG_PPC_BOOK3S_32
KBUILD_CFLAGS		+= -mcpu=powerpc
endif

# If we're on a ppc/ppc64/ppc64le machine use that defconfig, otherwise just use
# ppc64_defconfig because we have nothing better to go on.
uname := $(shell uname -m)
KBUILD_DEFCONFIG := $(if $(filter ppc%,$(uname)),$(uname),ppc64)_defconfig

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

# XXX This needs to be before we override LD below
ifdef CONFIG_PPC32
KBUILD_LDFLAGS_MODULE += arch/powerpc/lib/crtsavres.o
else
KBUILD_LDS_MODULE += $(srctree)/arch/powerpc/kernel/module.lds
ifeq ($(call ld-ifversion, -ge, 225000000, y),y)
# Have the linker provide sfpr if possible.
# There is a corresponding test in arch/powerpc/lib/Makefile
KBUILD_LDFLAGS_MODULE += --save-restore-funcs
else
KBUILD_LDFLAGS_MODULE += arch/powerpc/lib/crtsavres.o
endif
endif

ifdef CONFIG_CPU_LITTLE_ENDIAN
KBUILD_CFLAGS	+= -mlittle-endian
KBUILD_LDFLAGS	+= -EL
LDEMULATION	:= lppc
GNUTARGET	:= powerpcle
MULTIPLEWORD	:= -mno-multiple
KBUILD_CFLAGS_MODULE += $(call cc-option,-mno-save-toc-indirect)
else
KBUILD_CFLAGS += $(call cc-option,-mbig-endian)
KBUILD_LDFLAGS	+= -EB
LDEMULATION	:= ppc
GNUTARGET	:= powerpc
MULTIPLEWORD	:= -mmultiple
endif

ifdef CONFIG_PPC64
ifndef CONFIG_CC_IS_CLANG
cflags-$(CONFIG_CPU_BIG_ENDIAN)		+= $(call cc-option,-mabi=elfv1)
cflags-$(CONFIG_CPU_BIG_ENDIAN)		+= $(call cc-option,-mcall-aixdesc)
aflags-$(CONFIG_CPU_BIG_ENDIAN)		+= $(call cc-option,-mabi=elfv1)
aflags-$(CONFIG_CPU_LITTLE_ENDIAN)	+= -mabi=elfv2
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
KBUILD_CFLAGS	+= -m$(BITS)
KBUILD_AFLAGS	+= -m$(BITS) -Wl,-a$(BITS)
KBUILD_LDFLAGS	+= -m elf$(BITS)$(LDEMULATION)
endif

cflags-$(CONFIG_STACKPROTECTOR)	+= -mstack-protector-guard=tls
ifdef CONFIG_PPC64
cflags-$(CONFIG_STACKPROTECTOR)	+= -mstack-protector-guard-reg=r13
else
cflags-$(CONFIG_STACKPROTECTOR)	+= -mstack-protector-guard-reg=r2
endif

LDFLAGS_vmlinux-y := -Bstatic
LDFLAGS_vmlinux-$(CONFIG_RELOCATABLE) := -pie
LDFLAGS_vmlinux	:= $(LDFLAGS_vmlinux-y)
LDFLAGS_vmlinux += $(call ld-option,--orphan-handling=warn)

ifdef CONFIG_PPC64
ifeq ($(call cc-option-yn,-mcmodel=medium),y)
	# -mcmodel=medium breaks modules because it uses 32bit offsets from
	# the TOC pointer to create pointers where possible. Pointers into the
	# percpu data area are created by this method.
	#
	# The kernel module loader relocates the percpu data section from the
	# original location (starting with 0xd...) to somewhere in the base
	# kernel percpu data space (starting with 0xc...). We need a full
	# 64bit relocation for this to work, hence -mcmodel=large.
	KBUILD_CFLAGS_MODULE += -mcmodel=large
else
	export NO_MINIMAL_TOC := -mno-minimal-toc
endif
endif

CFLAGS-$(CONFIG_PPC64)	:= $(call cc-option,-mtraceback=no)
ifndef CONFIG_CC_IS_CLANG
ifdef CONFIG_CPU_LITTLE_ENDIAN
CFLAGS-$(CONFIG_PPC64)	+= $(call cc-option,-mabi=elfv2,$(call cc-option,-mcall-aixdesc))
AFLAGS-$(CONFIG_PPC64)	+= $(call cc-option,-mabi=elfv2)
else
CFLAGS-$(CONFIG_PPC64)	+= $(call cc-option,-mabi=elfv1)
CFLAGS-$(CONFIG_PPC64)	+= $(call cc-option,-mcall-aixdesc)
AFLAGS-$(CONFIG_PPC64)	+= $(call cc-option,-mabi=elfv1)
endif
endif
CFLAGS-$(CONFIG_PPC64)	+= $(call cc-option,-mcmodel=medium,$(call cc-option,-mminimal-toc))
CFLAGS-$(CONFIG_PPC64)	+= $(call cc-option,-mno-pointers-to-nested-functions)

# Clang unconditionally reserves r2 on ppc32 and does not support the flag
# https://bugs.llvm.org/show_bug.cgi?id=39555
CFLAGS-$(CONFIG_PPC32)	:= $(call cc-option, -ffixed-r2)

# Clang doesn't support -mmultiple / -mno-multiple
# https://bugs.llvm.org/show_bug.cgi?id=39556
CFLAGS-$(CONFIG_PPC32)	+= $(call cc-option, $(MULTIPLEWORD))

CFLAGS-$(CONFIG_PPC32)	+= $(call cc-option,-mno-readonly-in-sdata)

ifdef CONFIG_PPC_BOOK3S_64
ifdef CONFIG_CPU_LITTLE_ENDIAN
CFLAGS-$(CONFIG_GENERIC_CPU) += -mcpu=power8
CFLAGS-$(CONFIG_GENERIC_CPU) += $(call cc-option,-mtune=power9,-mtune=power8)
else
CFLAGS-$(CONFIG_GENERIC_CPU) += $(call cc-option,-mtune=power7,$(call cc-option,-mtune=power5))
CFLAGS-$(CONFIG_GENERIC_CPU) += $(call cc-option,-mcpu=power5,-mcpu=power4)
endif
else
CFLAGS-$(CONFIG_GENERIC_CPU) += -mcpu=powerpc64
endif

ifdef CONFIG_FUNCTION_TRACER
CC_FLAGS_FTRACE := -pg
ifdef CONFIG_MPROFILE_KERNEL
CC_FLAGS_FTRACE += -mprofile-kernel
endif
# Work around gcc code-gen bugs with -pg / -fno-omit-frame-pointer in gcc <= 4.8
# https://gcc.gnu.org/bugzilla/show_bug.cgi?id=44199
# https://gcc.gnu.org/bugzilla/show_bug.cgi?id=52828
ifndef CONFIG_CC_IS_CLANG
CC_FLAGS_FTRACE	+= $(call cc-ifversion, -lt, 0409, -mno-sched-epilog)
endif
endif

CFLAGS-$(CONFIG_TARGET_CPU_BOOL) += $(call cc-option,-mcpu=$(CONFIG_TARGET_CPU))

# Altivec option not allowed with e500mc64 in GCC.
ifdef CONFIG_ALTIVEC
E5500_CPU := -mcpu=powerpc64
else
E5500_CPU := $(call cc-option,-mcpu=e500mc64,-mcpu=powerpc64)
endif
CFLAGS-$(CONFIG_E5500_CPU) += $(E5500_CPU)
CFLAGS-$(CONFIG_E6500_CPU) += $(call cc-option,-mcpu=e6500,$(E5500_CPU))

ifdef CONFIG_PPC32
ifdef CONFIG_PPC_E500MC
CFLAGS-y += $(call cc-option,-mcpu=e500mc,-mcpu=powerpc)
else
CFLAGS-$(CONFIG_E500) += $(call cc-option,-mcpu=8540 -msoft-float,-mcpu=powerpc)
endif
endif

asinstr := $(call as-instr,lis 9$(comma)foo@high,-DHAVE_AS_ATHIGH=1)

KBUILD_CPPFLAGS	+= -I $(srctree)/arch/$(ARCH) $(asinstr)
KBUILD_AFLAGS	+= $(AFLAGS-y)
KBUILD_CFLAGS	+= $(call cc-option,-msoft-float)
KBUILD_CFLAGS	+= -pipe $(CFLAGS-y)
CPP		= $(CC) -E $(KBUILD_CFLAGS)

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

# No AltiVec or VSX instructions when building kernel
KBUILD_CFLAGS += $(call cc-option,-mno-altivec)
KBUILD_CFLAGS += $(call cc-option,-mno-vsx)

# No SPE instruction when building kernel
# (We use all available options to help semi-broken compilers)
KBUILD_CFLAGS += $(call cc-option,-mno-spe)
KBUILD_CFLAGS += $(call cc-option,-mspe=no)

# Don't emit .eh_frame since we have no use for it
KBUILD_CFLAGS += -fno-asynchronous-unwind-tables

# Never use string load/store instructions as they are
# often slow when they are implemented at all
KBUILD_CFLAGS		+= $(call cc-option,-mno-string)

cpu-as-$(CONFIG_40x)		+= -Wa,-m405
cpu-as-$(CONFIG_44x)		+= -Wa,-m440
cpu-as-$(CONFIG_ALTIVEC)	+= $(call as-option,-Wa$(comma)-maltivec)
cpu-as-$(CONFIG_E200)		+= -Wa,-me200
cpu-as-$(CONFIG_E500)		+= -Wa,-me500

# When using '-many -mpower4' gas will first try and find a matching power4
# mnemonic and failing that it will allow any valid mnemonic that GAS knows
# about. GCC will pass -many to GAS when assembling, clang does not.
cpu-as-$(CONFIG_PPC_BOOK3S_64)	+= -Wa,-mpower4 -Wa,-many
cpu-as-$(CONFIG_PPC_E500MC)	+= $(call as-option,-Wa$(comma)-me500mc)

KBUILD_AFLAGS += $(cpu-as-y)
KBUILD_CFLAGS += $(cpu-as-y)

KBUILD_AFLAGS += $(aflags-y)
KBUILD_CFLAGS += $(cflags-y)

head-$(CONFIG_PPC64)		:= arch/powerpc/kernel/head_64.o
head-$(CONFIG_PPC_BOOK3S_32)	:= arch/powerpc/kernel/head_book3s_32.o
head-$(CONFIG_PPC_8xx)		:= arch/powerpc/kernel/head_8xx.o
head-$(CONFIG_40x)		:= arch/powerpc/kernel/head_40x.o
head-$(CONFIG_44x)		:= arch/powerpc/kernel/head_44x.o
head-$(CONFIG_FSL_BOOKE)	:= arch/powerpc/kernel/head_fsl_booke.o

head-$(CONFIG_PPC64)		+= arch/powerpc/kernel/entry_64.o
head-$(CONFIG_PPC_FPU)		+= arch/powerpc/kernel/fpu.o
head-$(CONFIG_ALTIVEC)		+= arch/powerpc/kernel/vector.o
head-$(CONFIG_PPC_OF_BOOT_TRAMPOLINE)  += arch/powerpc/kernel/prom_init.o

# See arch/powerpc/Kbuild for content of core part of the kernel
core-y += arch/powerpc/

drivers-$(CONFIG_OPROFILE)	+= arch/powerpc/oprofile/

# Default to zImage, override when needed
all: zImage

# With make 3.82 we cannot mix normal and wildcard targets
BOOT_TARGETS1 := zImage zImage.initrd uImage
BOOT_TARGETS2 := zImage% dtbImage% treeImage.% cuImage.% simpleImage.% uImage.%

PHONY += $(BOOT_TARGETS1) $(BOOT_TARGETS2)

boot := arch/$(ARCH)/boot

$(BOOT_TARGETS1): vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(patsubst %,$(boot)/%,$@)
$(BOOT_TARGETS2): vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(patsubst %,$(boot)/%,$@)


PHONY += bootwrapper_install
bootwrapper_install:
	$(Q)$(MAKE) $(build)=$(boot) $(patsubst %,$(boot)/%,$@)

# Used to create 'merged defconfigs'
# To use it $(call) it with the first argument as the base defconfig
# and the second argument as a space separated list of .config files to merge,
# without the .config suffix.
define merge_into_defconfig
	$(Q)$(CONFIG_SHELL) $(srctree)/scripts/kconfig/merge_config.sh \
		-m -O $(objtree) $(srctree)/arch/$(ARCH)/configs/$(1) \
		$(foreach config,$(2),$(srctree)/arch/$(ARCH)/configs/$(config).config)
	+$(Q)$(MAKE) -f $(srctree)/Makefile olddefconfig
endef

PHONY += pseries_le_defconfig
pseries_le_defconfig:
	$(call merge_into_defconfig,pseries_defconfig,le)

PHONY += ppc64le_defconfig
ppc64le_defconfig:
	$(call merge_into_defconfig,ppc64_defconfig,le)

PHONY += ppc64le_guest_defconfig
ppc64le_guest_defconfig:
	$(call merge_into_defconfig,ppc64_defconfig,le guest)

PHONY += ppc64_guest_defconfig
ppc64_guest_defconfig:
	$(call merge_into_defconfig,ppc64_defconfig,be guest)

PHONY += powernv_be_defconfig
powernv_be_defconfig:
	$(call merge_into_defconfig,powernv_defconfig,be)

PHONY += mpc85xx_defconfig
mpc85xx_defconfig:
	$(call merge_into_defconfig,mpc85xx_base.config,\
		85xx-32bit 85xx-hw fsl-emb-nonhw)

PHONY += mpc85xx_smp_defconfig
mpc85xx_smp_defconfig:
	$(call merge_into_defconfig,mpc85xx_base.config,\
		85xx-32bit 85xx-smp 85xx-hw fsl-emb-nonhw)

PHONY += corenet32_smp_defconfig
corenet32_smp_defconfig:
	$(call merge_into_defconfig,corenet_base.config,\
		85xx-32bit 85xx-smp 85xx-hw fsl-emb-nonhw dpaa)

PHONY += corenet64_smp_defconfig
corenet64_smp_defconfig:
	$(call merge_into_defconfig,corenet_base.config,\
		85xx-64bit 85xx-smp altivec 85xx-hw fsl-emb-nonhw dpaa)

PHONY += mpc86xx_defconfig
mpc86xx_defconfig:
	$(call merge_into_defconfig,mpc86xx_base.config,\
		86xx-hw fsl-emb-nonhw)

PHONY += mpc86xx_smp_defconfig
mpc86xx_smp_defconfig:
	$(call merge_into_defconfig,mpc86xx_base.config,\
		86xx-smp 86xx-hw fsl-emb-nonhw)

PHONY += ppc32_allmodconfig
ppc32_allmodconfig:
	$(Q)$(MAKE) KCONFIG_ALLCONFIG=$(srctree)/arch/powerpc/configs/book3s_32.config \
		-f $(srctree)/Makefile allmodconfig

PHONY += ppc_defconfig
ppc_defconfig:
	$(call merge_into_defconfig,book3s_32.config,)

PHONY += ppc64le_allmodconfig
ppc64le_allmodconfig:
	$(Q)$(MAKE) KCONFIG_ALLCONFIG=$(srctree)/arch/powerpc/configs/le.config \
		-f $(srctree)/Makefile allmodconfig

PHONY += ppc64_book3e_allmodconfig
ppc64_book3e_allmodconfig:
	$(Q)$(MAKE) KCONFIG_ALLCONFIG=$(srctree)/arch/powerpc/configs/85xx-64bit.config \
		-f $(srctree)/Makefile allmodconfig

define archhelp
  @echo '* zImage          - Build default images selected by kernel config'
  @echo '  zImage.*        - Compressed kernel image (arch/$(ARCH)/boot/zImage.*)'
  @echo '  uImage          - U-Boot native image format'
  @echo '  cuImage.<dt>    - Backwards compatible U-Boot image for older'
  @echo '                    versions which do not support device trees'
  @echo '  dtbImage.<dt>   - zImage with an embedded device tree blob'
  @echo '  simpleImage.<dt> - Firmware independent image.'
  @echo '  treeImage.<dt>  - Support for older IBM 4xx firmware (not U-Boot)'
  @echo '  install         - Install kernel using'
  @echo '                    (your) ~/bin/$(INSTALLKERNEL) or'
  @echo '                    (distribution) /sbin/$(INSTALLKERNEL) or'
  @echo '                    install to $$(INSTALL_PATH) and run lilo'
  @echo '  *_defconfig     - Select default config from arch/$(ARCH)/configs'
  @echo ''
  @echo '  Targets with <dt> embed a device tree blob inside the image'
  @echo '  These targets support board with firmware that does not'
  @echo '  support passing a device tree directly.  Replace <dt> with the'
  @echo '  name of a dts file from the arch/$(ARCH)/boot/dts/ directory'
  @echo '  (minus the .dts extension).'
endef

PHONY += install
install:
	$(Q)$(MAKE) $(build)=$(boot) install

PHONY += vdso_install
vdso_install:
ifdef CONFIG_PPC64
	$(Q)$(MAKE) $(build)=arch/$(ARCH)/kernel/vdso64 $@
endif
ifdef CONFIG_VDSO32
	$(Q)$(MAKE) $(build)=arch/$(ARCH)/kernel/vdso32 $@
endif

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

archprepare: checkbin

archheaders:
	$(Q)$(MAKE) $(build)=arch/powerpc/kernel/syscalls all

ifdef CONFIG_STACKPROTECTOR
prepare: stack_protector_prepare

PHONY += stack_protector_prepare
stack_protector_prepare: prepare0
ifdef CONFIG_PPC64
	$(eval KBUILD_CFLAGS += -mstack-protector-guard-offset=$(shell awk '{if ($$2 == "PACA_CANARY") print $$3;}' include/generated/asm-offsets.h))
else
	$(eval KBUILD_CFLAGS += -mstack-protector-guard-offset=$(shell awk '{if ($$2 == "TASK_CANARY") print $$3;}' include/generated/asm-offsets.h))
endif
endif

ifdef CONFIG_SMP
prepare: task_cpu_prepare

PHONY += task_cpu_prepare
task_cpu_prepare: prepare0
	$(eval KBUILD_CFLAGS += -D_TASK_CPU=$(shell awk '{if ($$2 == "TASK_CPU") print $$3;}' include/generated/asm-offsets.h))
endif

PHONY += checkbin
# Check toolchain versions:
# - gcc-4.6 is the minimum kernel-wide version so nothing required.
checkbin:
	@if test "x${CONFIG_CPU_LITTLE_ENDIAN}" = "xy" \
	    && $(LD) --version | head -1 | grep ' 2\.24$$' >/dev/null ; then \
		echo -n '*** binutils 2.24 miscompiles weak symbols ' ; \
		echo 'in some circumstances.' ; \
		echo -n '*** Please use a different binutils version.' ; \
		false ; \
	fi
