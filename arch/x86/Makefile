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

# BITS is used as extension for files which are available in a 32 bit
# and a 64 bit version to simplify shared Makefiles.
# e.g.: obj-y += foo_$(BITS).o
export BITS

ifdef CONFIG_X86_NEED_RELOCS
        LDFLAGS_vmlinux := --emit-relocs
endif

ifeq ($(CONFIG_X86_32),y)
        BITS := 32
        UTS_MACHINE := i386
        CHECKFLAGS += -D__i386__

        biarch := $(call cc-option,-m32)
        KBUILD_AFLAGS += $(biarch)
        KBUILD_CFLAGS += $(biarch)

        KBUILD_CFLAGS += -msoft-float -mregparm=3 -freg-struct-return

        # Don't autogenerate MMX or SSE instructions
        KBUILD_CFLAGS += -mno-mmx -mno-sse

        # Never want PIC in a 32-bit kernel, prevent breakage with GCC built
        # with nonstandard options
        KBUILD_CFLAGS += -fno-pic

        # prevent gcc from keeping the stack 16 byte aligned
        KBUILD_CFLAGS += $(call cc-option,-mpreferred-stack-boundary=2)

        # Disable unit-at-a-time mode on pre-gcc-4.0 compilers, it makes gcc use
        # a lot more stack due to the lack of sharing of stacklots:
        KBUILD_CFLAGS += $(call cc-ifversion, -lt, 0400, \
				$(call cc-option,-fno-unit-at-a-time))

        # CPU-specific tuning. Anything which can be shared with UML should go here.
        include $(srctree)/arch/x86/Makefile_32.cpu
        KBUILD_CFLAGS += $(cflags-y)

        # temporary until string.h is fixed
        KBUILD_CFLAGS += -ffreestanding
else
        BITS := 64
        UTS_MACHINE := x86_64
        CHECKFLAGS += -D__x86_64__ -m64

        KBUILD_AFLAGS += -m64
        KBUILD_CFLAGS += -m64

        # Don't autogenerate MMX or SSE instructions
        KBUILD_CFLAGS += -mno-mmx -mno-sse

	# Use -mpreferred-stack-boundary=3 if supported.
	KBUILD_CFLAGS += $(call cc-option,-mpreferred-stack-boundary=3)

        # FIXME - should be integrated in Makefile.cpu (Makefile_32.cpu)
        cflags-$(CONFIG_MK8) += $(call cc-option,-march=k8)
        cflags-$(CONFIG_MPSC) += $(call cc-option,-march=nocona)

        cflags-$(CONFIG_MCORE2) += \
                $(call cc-option,-march=core2,$(call cc-option,-mtune=generic))
	cflags-$(CONFIG_MATOM) += $(call cc-option,-march=atom) \
		$(call cc-option,-mtune=atom,$(call cc-option,-mtune=generic))
        cflags-$(CONFIG_GENERIC_CPU) += $(call cc-option,-mtune=generic)
        KBUILD_CFLAGS += $(cflags-y)

        KBUILD_CFLAGS += -mno-red-zone
        KBUILD_CFLAGS += -mcmodel=kernel

        # -funit-at-a-time shrinks the kernel .text considerably
        # unfortunately it makes reading oopses harder.
        KBUILD_CFLAGS += $(call cc-option,-funit-at-a-time)

        # this works around some issues with generating unwind tables in older gccs
        # newer gccs do it by default
        KBUILD_CFLAGS += -maccumulate-outgoing-args
endif

ifdef CONFIG_CC_STACKPROTECTOR
	cc_has_sp := $(srctree)/scripts/gcc-x86_$(BITS)-has-stack-protector.sh
        ifeq ($(shell $(CONFIG_SHELL) $(cc_has_sp) $(CC) $(KBUILD_CPPFLAGS) $(biarch)),y)
                stackp-y := -fstack-protector
                KBUILD_CFLAGS += $(stackp-y)
        else
                $(warning stack protector enabled but no compiler support)
        endif
endif

ifdef CONFIG_X86_X32
	x32_ld_ok := $(call try-run,\
			/bin/echo -e '1: .quad 1b' | \
			$(CC) $(KBUILD_AFLAGS) -c -x assembler -o "$$TMP" - && \
			$(OBJCOPY) -O elf32-x86-64 "$$TMP" "$$TMPO" && \
			$(LD) -m elf32_x86_64 "$$TMPO" -o "$$TMP",y,n)
        ifeq ($(x32_ld_ok),y)
                CONFIG_X86_X32_ABI := y
                KBUILD_AFLAGS += -DCONFIG_X86_X32_ABI
                KBUILD_CFLAGS += -DCONFIG_X86_X32_ABI
        else
                $(warning CONFIG_X86_X32 enabled but no binutils support)
        endif
endif
export CONFIG_X86_X32_ABI

# Don't unroll struct assignments with kmemcheck enabled
ifeq ($(CONFIG_KMEMCHECK),y)
	KBUILD_CFLAGS += $(call cc-option,-fno-builtin-memcpy)
endif

# Stackpointer is addressed different for 32 bit and 64 bit x86
sp-$(CONFIG_X86_32) := esp
sp-$(CONFIG_X86_64) := rsp

# do binutils support CFI?
cfi := $(call as-instr,.cfi_startproc\n.cfi_rel_offset $(sp-y)$(comma)0\n.cfi_endproc,-DCONFIG_AS_CFI=1)
# is .cfi_signal_frame supported too?
cfi-sigframe := $(call as-instr,.cfi_startproc\n.cfi_signal_frame\n.cfi_endproc,-DCONFIG_AS_CFI_SIGNAL_FRAME=1)
cfi-sections := $(call as-instr,.cfi_sections .debug_frame,-DCONFIG_AS_CFI_SECTIONS=1)

# does binutils support specific instructions?
asinstr := $(call as-instr,fxsaveq (%rax),-DCONFIG_AS_FXSAVEQ=1)
avx_instr := $(call as-instr,vxorps %ymm0$(comma)%ymm1$(comma)%ymm2,-DCONFIG_AS_AVX=1)
avx2_instr :=$(call as-instr,vpbroadcastb %xmm0$(comma)%ymm1,-DCONFIG_AS_AVX2=1)

KBUILD_AFLAGS += $(cfi) $(cfi-sigframe) $(cfi-sections) $(asinstr) $(avx_instr) $(avx2_instr)
KBUILD_CFLAGS += $(cfi) $(cfi-sigframe) $(cfi-sections) $(asinstr) $(avx_instr) $(avx2_instr)

LDFLAGS := -m elf_$(UTS_MACHINE)

# Speed up the build
KBUILD_CFLAGS += -pipe
# Workaround for a gcc prelease that unfortunately was shipped in a suse release
KBUILD_CFLAGS += -Wno-sign-compare
#
KBUILD_CFLAGS += -fno-asynchronous-unwind-tables
# prevent gcc from generating any FP code by mistake
KBUILD_CFLAGS += $(call cc-option,-mno-sse -mno-mmx -mno-sse2 -mno-3dnow,)
KBUILD_CFLAGS += $(call cc-option,-mno-avx,)

KBUILD_CFLAGS += $(mflags-y)
KBUILD_AFLAGS += $(mflags-y)

archscripts: scripts_basic
	$(Q)$(MAKE) $(build)=arch/x86/tools relocs

###
# Syscall table generation

archheaders:
	$(Q)$(MAKE) $(build)=arch/x86/syscalls all

###
# Kernel objects

head-y := arch/x86/kernel/head_$(BITS).o
head-y += arch/x86/kernel/head$(BITS).o
head-y += arch/x86/kernel/head.o

libs-y  += arch/x86/lib/

# See arch/x86/Kbuild for content of core part of the kernel
core-y += arch/x86/

# drivers-y are linked after core-y
drivers-$(CONFIG_MATH_EMULATION) += arch/x86/math-emu/
drivers-$(CONFIG_PCI)            += arch/x86/pci/

# must be linked after kernel/
drivers-$(CONFIG_OPROFILE) += arch/x86/oprofile/

# suspend and hibernation support
drivers-$(CONFIG_PM) += arch/x86/power/

drivers-$(CONFIG_FB) += arch/x86/video/

####
# boot loader support. Several targets are kept for legacy purposes

boot := arch/x86/boot

BOOT_TARGETS = bzlilo bzdisk fdimage fdimage144 fdimage288 isoimage

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
	$(Q)$(MAKE) $(build)=$(boot) $@

PHONY += vdso_install
vdso_install:
	$(Q)$(MAKE) $(build)=arch/x86/vdso $@

archclean:
	$(Q)rm -rf $(objtree)/arch/i386
	$(Q)rm -rf $(objtree)/arch/x86_64
	$(Q)$(MAKE) $(clean)=$(boot)
	$(Q)$(MAKE) $(clean)=arch/x86/tools

PHONY += kvmconfig
kvmconfig:
	$(if $(wildcard $(objtree)/.config),, $(error You need an existing .config for this target))
	$(Q)$(CONFIG_SHELL) $(srctree)/scripts/kconfig/merge_config.sh -m -O $(objtree) $(objtree)/.config arch/x86/configs/kvm_guest.config
	$(Q)yes "" | $(MAKE) oldconfig

define archhelp
  echo  '* bzImage      - Compressed kernel image (arch/x86/boot/bzImage)'
  echo  '  install      - Install kernel using'
  echo  '                  (your) ~/bin/$(INSTALLKERNEL) or'
  echo  '                  (distribution) /sbin/$(INSTALLKERNEL) or'
  echo  '                  install to $$(INSTALL_PATH) and run lilo'
  echo  '  fdimage      - Create 1.4MB boot floppy image (arch/x86/boot/fdimage)'
  echo  '  fdimage144   - Create 1.4MB boot floppy image (arch/x86/boot/fdimage)'
  echo  '  fdimage288   - Create 2.8MB boot floppy image (arch/x86/boot/fdimage)'
  echo  '  isoimage     - Create a boot CD-ROM image (arch/x86/boot/image.iso)'
  echo  '                  bzdisk/fdimage*/isoimage also accept:'
  echo  '                  FDARGS="..."  arguments for the booted kernel'
  echo  '                  FDINITRD=file initrd for the booted kernel'
  echo  '  kvmconfig	- Enable additional options for guest kernel support'
endef
