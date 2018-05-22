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

CROSS32CC		:= $(CROSS32_COMPILE)gcc
CROSS32AR		:= $(CROSS32_COMPILE)ar

ifeq ($(HAS_BIARCH),y)
ifeq ($(CROSS32_COMPILE),)
CROSS32CC	:= $(CC) -m32
KBUILD_ARFLAGS	+= --target=elf32-powerpc
endif
endif

export CROSS32CC CROSS32AR

ifeq ($(CROSS_COMPILE),)
KBUILD_DEFCONFIG := $(shell uname -m)_defconfig
else
KBUILD_DEFCONFIG := ppc64_defconfig
endif

ifeq ($(CONFIG_PPC64),y)
new_nm := $(shell if $(NM) --help 2>&1 | grep -- '--synthetic' > /dev/null; then echo y; else echo n; fi)

ifeq ($(new_nm),y)
NM		:= $(NM) --synthetic
endif
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
KBUILD_LDFLAGS_MODULE += -T $(srctree)/arch/powerpc/kernel/module.lds
ifeq ($(call ld-ifversion, -ge, 225000000, y),y)
# Have the linker provide sfpr if possible.
# There is a corresponding test in arch/powerpc/lib/Makefile
KBUILD_LDFLAGS_MODULE += --save-restore-funcs
else
KBUILD_LDFLAGS_MODULE += arch/powerpc/lib/crtsavres.o
endif
endif

ifeq ($(CONFIG_CPU_LITTLE_ENDIAN),y)
override LD	+= -EL
LDEMULATION	:= lppc
GNUTARGET	:= powerpcle
MULTIPLEWORD	:= -mno-multiple
KBUILD_CFLAGS_MODULE += $(call cc-option,-mno-save-toc-indirect)
else
override LD	+= -EB
LDEMULATION	:= ppc
GNUTARGET	:= powerpc
MULTIPLEWORD	:= -mmultiple
endif

ifdef CONFIG_PPC64
cflags-$(CONFIG_CPU_BIG_ENDIAN)		+= $(call cc-option,-mabi=elfv1)
cflags-$(CONFIG_CPU_BIG_ENDIAN)		+= $(call cc-option,-mcall-aixdesc)
aflags-$(CONFIG_CPU_BIG_ENDIAN)		+= $(call cc-option,-mabi=elfv1)
aflags-$(CONFIG_CPU_LITTLE_ENDIAN)	+= -mabi=elfv2
endif

cflags-$(CONFIG_CPU_LITTLE_ENDIAN)	+= -mlittle-endian
cflags-$(CONFIG_CPU_BIG_ENDIAN)		+= $(call cc-option,-mbig-endian)
ifneq ($(cc-name),clang)
  cflags-$(CONFIG_CPU_LITTLE_ENDIAN)	+= -mno-strict-align
endif

aflags-$(CONFIG_CPU_BIG_ENDIAN)		+= $(call cc-option,-mbig-endian)
aflags-$(CONFIG_CPU_LITTLE_ENDIAN)	+= -mlittle-endian

ifeq ($(HAS_BIARCH),y)
override AS	+= -a$(BITS)
override LD	+= -m elf$(BITS)$(LDEMULATION)
override CC	+= -m$(BITS)
KBUILD_ARFLAGS	+= --target=elf$(BITS)-$(GNUTARGET)
endif

LDFLAGS_vmlinux-y := -Bstatic
LDFLAGS_vmlinux-$(CONFIG_RELOCATABLE) := -pie
LDFLAGS_vmlinux	:= $(LDFLAGS_vmlinux-y)
LDFLAGS_vmlinux += $(call ld-option,--orphan-handling=warn)

ifeq ($(CONFIG_PPC64),y)
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
ifeq ($(CONFIG_CPU_LITTLE_ENDIAN),y)
CFLAGS-$(CONFIG_PPC64)	+= $(call cc-option,-mabi=elfv2,$(call cc-option,-mcall-aixdesc))
AFLAGS-$(CONFIG_PPC64)	+= $(call cc-option,-mabi=elfv2)
else
CFLAGS-$(CONFIG_PPC64)	+= $(call cc-option,-mabi=elfv1)
CFLAGS-$(CONFIG_PPC64)	+= $(call cc-option,-mcall-aixdesc)
AFLAGS-$(CONFIG_PPC64)	+= $(call cc-option,-mabi=elfv1)
endif
CFLAGS-$(CONFIG_PPC64)	+= $(call cc-option,-mcmodel=medium,$(call cc-option,-mminimal-toc))
CFLAGS-$(CONFIG_PPC64)	+= $(call cc-option,-mno-pointers-to-nested-functions)
CFLAGS-$(CONFIG_PPC32)	:= -ffixed-r2 $(MULTIPLEWORD)

ifeq ($(CONFIG_PPC_BOOK3S_64),y)
CFLAGS-$(CONFIG_GENERIC_CPU) += $(call cc-option,-mtune=power7,-mtune=power4)
CFLAGS-$(CONFIG_GENERIC_CPU) += -mcpu=power4
else
CFLAGS-$(CONFIG_GENERIC_CPU) += -mcpu=powerpc64
endif

ifdef CONFIG_MPROFILE_KERNEL
    ifeq ($(shell $(srctree)/arch/powerpc/tools/gcc-check-mprofile-kernel.sh $(CC) -I$(srctree)/include -D__KERNEL__),OK)
        CC_FLAGS_FTRACE := -pg -mprofile-kernel
        KBUILD_CPPFLAGS += -DCC_USING_MPROFILE_KERNEL
    else
        # If the user asked for mprofile-kernel but the toolchain doesn't
        # support it, emit a warning and deliberately break the build later
        # with mprofile-kernel-not-supported. We would prefer to make this an
        # error right here, but then the user would never be able to run
        # oldconfig to change their configuration.
        $(warning Compiler does not support mprofile-kernel, set CONFIG_DISABLE_MPROFILE_KERNEL)
        CC_FLAGS_FTRACE := -mprofile-kernel-not-supported
    endif
endif

CFLAGS-$(CONFIG_CELL_CPU) += $(call cc-option,-mcpu=cell)
CFLAGS-$(CONFIG_POWER4_CPU) += $(call cc-option,-mcpu=power4)
CFLAGS-$(CONFIG_POWER5_CPU) += $(call cc-option,-mcpu=power5)
CFLAGS-$(CONFIG_POWER6_CPU) += $(call cc-option,-mcpu=power6)
CFLAGS-$(CONFIG_POWER7_CPU) += $(call cc-option,-mcpu=power7)
CFLAGS-$(CONFIG_POWER8_CPU) += $(call cc-option,-mcpu=power8)

# Altivec option not allowed with e500mc64 in GCC.
ifeq ($(CONFIG_ALTIVEC),y)
E5500_CPU := -mcpu=powerpc64
else
E5500_CPU := $(call cc-option,-mcpu=e500mc64,-mcpu=powerpc64)
endif
CFLAGS-$(CONFIG_E5500_CPU) += $(E5500_CPU)
CFLAGS-$(CONFIG_E6500_CPU) += $(call cc-option,-mcpu=e6500,$(E5500_CPU))

ifeq ($(CONFIG_PPC32),y)
ifeq ($(CONFIG_PPC_E500MC),y)
CFLAGS-y += $(call cc-option,-mcpu=e500mc,-mcpu=powerpc)
else
CFLAGS-$(CONFIG_E500) += $(call cc-option,-mcpu=8540 -msoft-float,-mcpu=powerpc)
endif
endif

asinstr := $(call as-instr,lis 9$(comma)foo@high,-DHAVE_AS_ATHIGH=1)

KBUILD_CPPFLAGS	+= -Iarch/$(ARCH) $(asinstr)
KBUILD_AFLAGS	+= -Iarch/$(ARCH) $(AFLAGS-y)
KBUILD_CFLAGS	+= $(call cc-option,-msoft-float)
KBUILD_CFLAGS	+= -pipe -Iarch/$(ARCH) $(CFLAGS-y)
CPP		= $(CC) -E $(KBUILD_CFLAGS)

CHECKFLAGS	+= -m$(BITS) -D__powerpc__ -D__powerpc$(BITS)__
ifdef CONFIG_CPU_BIG_ENDIAN
CHECKFLAGS	+= -D__BIG_ENDIAN__
else
CHECKFLAGS	+= -D__LITTLE_ENDIAN__
endif

ifeq ($(CONFIG_476FPE_ERR46),y)
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

# Enable unit-at-a-time mode when possible. It shrinks the
# kernel considerably.
KBUILD_CFLAGS += $(call cc-option,-funit-at-a-time)

# FIXME: the module load should be taught about the additional relocs
# generated by this.
# revert to pre-gcc-4.4 behaviour of .eh_frame
KBUILD_CFLAGS	+= $(call cc-option,-fno-dwarf2-cfi-asm)

# Never use string load/store instructions as they are
# often slow when they are implemented at all
KBUILD_CFLAGS		+= $(call cc-option,-mno-string)

ifeq ($(CONFIG_6xx),y)
KBUILD_CFLAGS		+= -mcpu=powerpc
endif

# Work around a gcc code-gen bug with -fno-omit-frame-pointer.
ifeq ($(CONFIG_FUNCTION_TRACER),y)
KBUILD_CFLAGS		+= -mno-sched-epilog
endif

cpu-as-$(CONFIG_4xx)		+= -Wa,-m405
cpu-as-$(CONFIG_ALTIVEC)	+= $(call as-option,-Wa$(comma)-maltivec)
cpu-as-$(CONFIG_E200)		+= -Wa,-me200

KBUILD_AFLAGS += $(cpu-as-y)
KBUILD_CFLAGS += $(cpu-as-y)

KBUILD_AFLAGS += $(aflags-y)
KBUILD_CFLAGS += $(cflags-y)

head-y				:= arch/powerpc/kernel/head_$(BITS).o
head-$(CONFIG_PPC_8xx)		:= arch/powerpc/kernel/head_8xx.o
head-$(CONFIG_40x)		:= arch/powerpc/kernel/head_40x.o
head-$(CONFIG_44x)		:= arch/powerpc/kernel/head_44x.o
head-$(CONFIG_FSL_BOOKE)	:= arch/powerpc/kernel/head_fsl_booke.o

head-$(CONFIG_PPC64)		+= arch/powerpc/kernel/entry_64.o
head-$(CONFIG_PPC_FPU)		+= arch/powerpc/kernel/fpu.o
head-$(CONFIG_ALTIVEC)		+= arch/powerpc/kernel/vector.o
head-$(CONFIG_PPC_OF_BOOT_TRAMPOLINE)  += arch/powerpc/kernel/prom_init.o

core-y				+= arch/powerpc/kernel/ \
				   arch/powerpc/mm/ \
				   arch/powerpc/lib/ \
				   arch/powerpc/sysdev/ \
				   arch/powerpc/platforms/ \
				   arch/powerpc/math-emu/ \
				   arch/powerpc/crypto/ \
				   arch/powerpc/net/
core-$(CONFIG_XMON)		+= arch/powerpc/xmon/
core-$(CONFIG_KVM) 		+= arch/powerpc/kvm/
core-$(CONFIG_PERF_EVENTS)	+= arch/powerpc/perf/
core-$(CONFIG_KEXEC_FILE)	+= arch/powerpc/purgatory/

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


bootwrapper_install:
	$(Q)$(MAKE) $(build)=$(boot) $(patsubst %,$(boot)/%,$@)

%.dtb: scripts
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

PHONY += powernv_be_defconfig
powernv_be_defconfig:
	$(call merge_into_defconfig,powernv_defconfig,be)

PHONY += mpc85xx_defconfig
mpc85xx_defconfig:
	$(call merge_into_defconfig,mpc85xx_basic_defconfig,\
		85xx-32bit 85xx-hw fsl-emb-nonhw)

PHONY += mpc85xx_smp_defconfig
mpc85xx_smp_defconfig:
	$(call merge_into_defconfig,mpc85xx_basic_defconfig,\
		85xx-32bit 85xx-smp 85xx-hw fsl-emb-nonhw)

PHONY += corenet32_smp_defconfig
corenet32_smp_defconfig:
	$(call merge_into_defconfig,corenet_basic_defconfig,\
		85xx-32bit 85xx-smp 85xx-hw fsl-emb-nonhw dpaa)

PHONY += corenet64_smp_defconfig
corenet64_smp_defconfig:
	$(call merge_into_defconfig,corenet_basic_defconfig,\
		85xx-64bit 85xx-smp altivec 85xx-hw fsl-emb-nonhw dpaa)

PHONY += mpc86xx_defconfig
mpc86xx_defconfig:
	$(call merge_into_defconfig,mpc86xx_basic_defconfig,\
		86xx-hw fsl-emb-nonhw)

PHONY += mpc86xx_smp_defconfig
mpc86xx_smp_defconfig:
	$(call merge_into_defconfig,mpc86xx_basic_defconfig,\
		86xx-smp 86xx-hw fsl-emb-nonhw)

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

install:
	$(Q)$(MAKE) $(build)=$(boot) install

vdso_install:
ifeq ($(CONFIG_PPC64),y)
	$(Q)$(MAKE) $(build)=arch/$(ARCH)/kernel/vdso64 $@
endif
	$(Q)$(MAKE) $(build)=arch/$(ARCH)/kernel/vdso32 $@

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

archprepare: checkbin

# Use the file '.tmp_gas_check' for binutils tests, as gas won't output
# to stdout and these checks are run even on install targets.
TOUT	:= .tmp_gas_check

# Check gcc and binutils versions:
# - gcc-3.4 and binutils-2.14 are a fatal combination
# - Require gcc 4.0 or above on 64-bit
# - gcc-4.2.0 has issues compiling modules on 64-bit
checkbin:
	@if test "$(cc-name)" != "clang" \
	    && test "$(cc-version)" = "0304" ; then \
		if ! /bin/echo mftb 5 | $(AS) -v -mppc -many -o $(TOUT) >/dev/null 2>&1 ; then \
			echo -n '*** ${VERSION}.${PATCHLEVEL} kernels no longer build '; \
			echo 'correctly with gcc-3.4 and your version of binutils.'; \
			echo '*** Please upgrade your binutils or downgrade your gcc'; \
			false; \
		fi ; \
	fi
	@if test "$(cc-name)" != "clang" \
	    && test "$(cc-version)" -lt "0400" \
	    && test "x${CONFIG_PPC64}" = "xy" ; then \
                echo -n "Sorry, GCC v4.0 or above is required to build " ; \
                echo "the 64-bit powerpc kernel." ; \
                false ; \
        fi
	@if test "$(cc-name)" != "clang" \
	    && test "$(cc-fullversion)" = "040200" \
	    && test "x${CONFIG_MODULES}${CONFIG_PPC64}" = "xyy" ; then \
		echo -n '*** GCC-4.2.0 cannot compile the 64-bit powerpc ' ; \
		echo 'kernel with modules enabled.' ; \
		echo -n '*** Please use a different GCC version or ' ; \
		echo 'disable kernel modules' ; \
		false ; \
	fi
	@if test "x${CONFIG_CPU_LITTLE_ENDIAN}" = "xy" \
	    && $(LD) --version | head -1 | grep ' 2\.24$$' >/dev/null ; then \
		echo -n '*** binutils 2.24 miscompiles weak symbols ' ; \
		echo 'in some circumstances.' ; \
		echo -n '*** Please use a different binutils version.' ; \
		false ; \
	fi


CLEAN_FILES += $(TOUT)

