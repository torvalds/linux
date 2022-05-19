# SPDX-License-Identifier: GPL-2.0
# Unified Makefile for i386 and x86_64

# select defconfig based on actual architecture
ifeq ($(ARCH),x86)
  ifeq ($(shell uname -m),x86_64)
        KBUILD_DEFCONFIG := x86_64_defconfig
  else
        KBUILD_DEFCONFIG := i386_defconfig
  endif
else
        KBUILD_DEFCONFIG := $(ARCH)_defconfig
endif

# For gcc stack alignment is specified with -mpreferred-stack-boundary,
# clang has the option -mstack-alignment for that purpose.
ifneq ($(call cc-option, -mpreferred-stack-boundary=4),)
      cc_stack_align4 := -mpreferred-stack-boundary=2
      cc_stack_align8 := -mpreferred-stack-boundary=3
else ifneq ($(call cc-option, -mstack-alignment=16),)
      cc_stack_align4 := -mstack-alignment=4
      cc_stack_align8 := -mstack-alignment=8
endif

# How to compile the 16-bit code.  Note we always compile for -march=i386;
# that way we can complain to the user if the CPU is insufficient.
REALMODE_CFLAGS	:= -m16 -g -Os -DDISABLE_BRANCH_PROFILING \
		   -Wall -Wstrict-prototypes -march=i386 -mregparm=3 \
		   -fno-strict-aliasing -fomit-frame-pointer -fno-pic \
		   -mno-mmx -mno-sse $(call cc-option,-fcf-protection=none)

REALMODE_CFLAGS += -ffreestanding
REALMODE_CFLAGS += -fno-stack-protector
REALMODE_CFLAGS += -Wno-address-of-packed-member
REALMODE_CFLAGS += $(cc_stack_align4)
REALMODE_CFLAGS += $(CLANG_FLAGS)
export REALMODE_CFLAGS

# BITS is used as extension for files which are available in a 32 bit
# and a 64 bit version to simplify shared Makefiles.
# e.g.: obj-y += foo_$(BITS).o
export BITS

#
# Prevent GCC from generating any FP code by mistake.
#
# This must happen before we try the -mpreferred-stack-boundary, see:
#
#    https://gcc.gnu.org/bugzilla/show_bug.cgi?id=53383
#
KBUILD_CFLAGS += -mno-sse -mno-mmx -mno-sse2 -mno-3dnow -mno-avx

# Intel CET isn't enabled in the kernel
KBUILD_CFLAGS += $(call cc-option,-fcf-protection=none)

ifeq ($(CONFIG_X86_32),y)
        BITS := 32
        UTS_MACHINE := i386
        CHECKFLAGS += -D__i386__

        KBUILD_AFLAGS += -m32
        KBUILD_CFLAGS += -m32

        KBUILD_CFLAGS += -msoft-float -mregparm=3 -freg-struct-return

        # Never want PIC in a 32-bit kernel, prevent breakage with GCC built
        # with nonstandard options
        KBUILD_CFLAGS += -fno-pic

        # Align the stack to the register width instead of using the default
        # alignment of 16 bytes. This reduces stack usage and the number of
        # alignment instructions.
        KBUILD_CFLAGS += $(cc_stack_align4)

        # CPU-specific tuning. Anything which can be shared with UML should go here.
        include $(srctree)/arch/x86/Makefile_32.cpu
        KBUILD_CFLAGS += $(cflags-y)

        # temporary until string.h is fixed
        KBUILD_CFLAGS += -ffreestanding

	ifeq ($(CONFIG_STACKPROTECTOR),y)
		ifeq ($(CONFIG_SMP),y)
			KBUILD_CFLAGS += -mstack-protector-guard-reg=fs -mstack-protector-guard-symbol=__stack_chk_guard
		else
			KBUILD_CFLAGS += -mstack-protector-guard=global
		endif
	endif
else
        BITS := 64
        UTS_MACHINE := x86_64
        CHECKFLAGS += -D__x86_64__

        KBUILD_AFLAGS += -m64
        KBUILD_CFLAGS += -m64

        # Align jump targets to 1 byte, not the default 16 bytes:
        KBUILD_CFLAGS += $(call cc-option,-falign-jumps=1)

        # Pack loops tightly as well:
        KBUILD_CFLAGS += $(call cc-option,-falign-loops=1)

        # Don't autogenerate traditional x87 instructions
        KBUILD_CFLAGS += -mno-80387
        KBUILD_CFLAGS += $(call cc-option,-mno-fp-ret-in-387)

        # By default gcc and clang use a stack alignment of 16 bytes for x86.
        # However the standard kernel entry on x86-64 leaves the stack on an
        # 8-byte boundary. If the compiler isn't informed about the actual
        # alignment it will generate extra alignment instructions for the
        # default alignment which keep the stack *mis*aligned.
        # Furthermore an alignment to the register width reduces stack usage
        # and the number of alignment instructions.
        KBUILD_CFLAGS += $(cc_stack_align8)

	# Use -mskip-rax-setup if supported.
	KBUILD_CFLAGS += $(call cc-option,-mskip-rax-setup)

        # FIXME - should be integrated in Makefile.cpu (Makefile_32.cpu)
        cflags-$(CONFIG_MK8)		+= -march=k8
        cflags-$(CONFIG_MPSC)		+= -march=nocona
        cflags-$(CONFIG_MCORE2)		+= -march=core2
        cflags-$(CONFIG_MATOM)		+= -march=atom
        cflags-$(CONFIG_GENERIC_CPU)	+= -mtune=generic
        KBUILD_CFLAGS += $(cflags-y)

        KBUILD_CFLAGS += -mno-red-zone
        KBUILD_CFLAGS += -mcmodel=kernel
endif

ifdef CONFIG_X86_X32
	x32_ld_ok := $(call try-run,\
			/bin/echo -e '1: .quad 1b' | \
			$(CC) $(KBUILD_AFLAGS) -c -x assembler -o "$$TMP" - && \
			$(OBJCOPY) -O elf32-x86-64 "$$TMP" "$$TMP.o" && \
			$(LD) -m elf32_x86_64 "$$TMP.o" -o "$$TMP",y,n)
        ifeq ($(x32_ld_ok),y)
                CONFIG_X86_X32_ABI := y
                KBUILD_AFLAGS += -DCONFIG_X86_X32_ABI
                KBUILD_CFLAGS += -DCONFIG_X86_X32_ABI
        else
                $(warning CONFIG_X86_X32 enabled but no binutils support)
        endif
endif
export CONFIG_X86_X32_ABI

#
# If the function graph tracer is used with mcount instead of fentry,
# '-maccumulate-outgoing-args' is needed to prevent a GCC bug
# (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=42109)
#
ifdef CONFIG_FUNCTION_GRAPH_TRACER
  ifndef CONFIG_HAVE_FENTRY
	ACCUMULATE_OUTGOING_ARGS := 1
  endif
endif

ifeq ($(ACCUMULATE_OUTGOING_ARGS), 1)
	# This compiler flag is not supported by Clang:
	KBUILD_CFLAGS += $(call cc-option,-maccumulate-outgoing-args,)
endif

# Workaround for a gcc prelease that unfortunately was shipped in a suse release
KBUILD_CFLAGS += -Wno-sign-compare
#
KBUILD_CFLAGS += -fno-asynchronous-unwind-tables

# Avoid indirect branches in kernel to deal with Spectre
ifdef CONFIG_RETPOLINE
  KBUILD_CFLAGS += $(RETPOLINE_CFLAGS)
  # Additionally, avoid generating expensive indirect jumps which
  # are subject to retpolines for small number of switch cases.
  # clang turns off jump table generation by default when under
  # retpoline builds, however, gcc does not for x86. This has
  # only been fixed starting from gcc stable version 8.4.0 and
  # onwards, but not for older ones. See gcc bug #86952.
  ifndef CONFIG_CC_IS_CLANG
    KBUILD_CFLAGS += -fno-jump-tables
  endif
endif

ifdef CONFIG_SLS
  KBUILD_CFLAGS += -mharden-sls=all
endif

KBUILD_LDFLAGS += -m elf_$(UTS_MACHINE)

ifdef CONFIG_LTO_CLANG
ifeq ($(shell test $(CONFIG_LLD_VERSION) -lt 130000; echo $$?),0)
KBUILD_LDFLAGS	+= -plugin-opt=-stack-alignment=$(if $(CONFIG_X86_32),4,8)
endif
endif

ifdef CONFIG_X86_NEED_RELOCS
LDFLAGS_vmlinux := --emit-relocs --discard-none
else
LDFLAGS_vmlinux :=
endif

#
# The 64-bit kernel must be aligned to 2MB.  Pass -z max-page-size=0x200000 to
# the linker to force 2MB page size regardless of the default page size used
# by the linker.
#
ifdef CONFIG_X86_64
LDFLAGS_vmlinux += -z max-page-size=0x200000
endif


archscripts: scripts_basic
	$(Q)$(MAKE) $(build)=arch/x86/tools relocs

###
# Syscall table generation

archheaders:
	$(Q)$(MAKE) $(build)=arch/x86/entry/syscalls all

###
# Kernel objects

head-y := arch/x86/kernel/head_$(BITS).o
head-y += arch/x86/kernel/head$(BITS).o
head-y += arch/x86/kernel/ebda.o
head-y += arch/x86/kernel/platform-quirks.o

libs-y  += arch/x86/lib/

# drivers-y are linked after core-y
drivers-$(CONFIG_MATH_EMULATION) += arch/x86/math-emu/
drivers-$(CONFIG_PCI)            += arch/x86/pci/

# suspend and hibernation support
drivers-$(CONFIG_PM) += arch/x86/power/

drivers-$(CONFIG_FB) += arch/x86/video/

####
# boot loader support. Several targets are kept for legacy purposes

boot := arch/x86/boot

BOOT_TARGETS = bzdisk fdimage fdimage144 fdimage288 hdimage isoimage

PHONY += bzImage $(BOOT_TARGETS)

# Default kernel to build
all: bzImage

# KBUILD_IMAGE specify target image being built
KBUILD_IMAGE := $(boot)/bzImage

bzImage: vmlinux
ifeq ($(CONFIG_X86_DECODER_SELFTEST),y)
	$(Q)$(MAKE) $(build)=arch/x86/tools posttest
endif
	$(Q)$(MAKE) $(build)=$(boot) $(KBUILD_IMAGE)
	$(Q)mkdir -p $(objtree)/arch/$(UTS_MACHINE)/boot
	$(Q)ln -fsn ../../x86/boot/bzImage $(objtree)/arch/$(UTS_MACHINE)/boot/$@

$(BOOT_TARGETS): vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $@

PHONY += install
install:
	$(CONFIG_SHELL) $(srctree)/$(boot)/install.sh $(KERNELRELEASE) \
		$(KBUILD_IMAGE) System.map "$(INSTALL_PATH)"

PHONY += vdso_install
vdso_install:
	$(Q)$(MAKE) $(build)=arch/x86/entry/vdso $@

archprepare: checkbin
checkbin:
ifndef CONFIG_CC_HAS_ASM_GOTO
	@echo Compiler lacks asm-goto support.
	@exit 1
endif
ifdef CONFIG_RETPOLINE
ifeq ($(RETPOLINE_CFLAGS),)
	@echo "You are building kernel with non-retpoline compiler." >&2
	@echo "Please update your compiler." >&2
	@false
endif
endif

archclean:
	$(Q)rm -rf $(objtree)/arch/i386
	$(Q)rm -rf $(objtree)/arch/x86_64
	$(Q)$(MAKE) $(clean)=$(boot)
	$(Q)$(MAKE) $(clean)=arch/x86/tools

define archhelp
  echo  '* bzImage		- Compressed kernel image (arch/x86/boot/bzImage)'
  echo  '  install		- Install kernel using (your) ~/bin/$(INSTALLKERNEL) or'
  echo  '			  (distribution) /sbin/$(INSTALLKERNEL) or install to '
  echo  '			  $$(INSTALL_PATH) and run lilo'
  echo  ''
  echo  '  fdimage		- Create 1.4MB boot floppy image (arch/x86/boot/fdimage)'
  echo  '  fdimage144		- Create 1.4MB boot floppy image (arch/x86/boot/fdimage)'
  echo  '  fdimage288		- Create 2.8MB boot floppy image (arch/x86/boot/fdimage)'
  echo  '  hdimage		- Create a BIOS/EFI hard disk image (arch/x86/boot/hdimage)'
  echo  '  isoimage		- Create a boot CD-ROM image (arch/x86/boot/image.iso)'
  echo  '			  bzdisk/fdimage*/hdimage/isoimage also accept:'
  echo  '			  FDARGS="..."  arguments for the booted kernel'
  echo  '                  	  FDINITRD=file initrd for the booted kernel'
  echo  ''
  echo  '  kvm_guest.config	- Enable Kconfig items for running this kernel as a KVM guest'
  echo  '  xen.config		- Enable Kconfig items for running this kernel as a Xen guest'

endef
