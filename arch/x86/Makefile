# Unified Makefile for i386 and x86_64

# select defconfig based on actual architecture
ifeq ($(ARCH),x86)
        KBUILD_DEFCONFIG := i386_defconfig
else
        KBUILD_DEFCONFIG := $(ARCH)_defconfig
endif

core-$(CONFIG_KVM) += arch/x86/kvm/

# BITS is used as extension for files which are available in a 32 bit
# and a 64 bit version to simplify shared Makefiles.
# e.g.: obj-y += foo_$(BITS).o
export BITS

ifeq ($(CONFIG_X86_32),y)
        BITS := 32
        UTS_MACHINE := i386
        CHECKFLAGS += -D__i386__

        biarch := $(call cc-option,-m32)
        KBUILD_AFLAGS += $(biarch)
        KBUILD_CFLAGS += $(biarch)

        ifdef CONFIG_RELOCATABLE
                LDFLAGS_vmlinux := --emit-relocs
        endif

        KBUILD_CFLAGS += -msoft-float -mregparm=3 -freg-struct-return

        # prevent gcc from keeping the stack 16 byte aligned
        KBUILD_CFLAGS += $(call cc-option,-mpreferred-stack-boundary=2)

        # Disable unit-at-a-time mode on pre-gcc-4.0 compilers, it makes gcc use
        # a lot more stack due to the lack of sharing of stacklots:
        KBUILD_CFLAGS += $(shell if [ $(call cc-version) -lt 0400 ] ; then \
                echo $(call cc-option,-fno-unit-at-a-time); fi ;)

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

        # FIXME - should be integrated in Makefile.cpu (Makefile_32.cpu)
        cflags-$(CONFIG_MK8) += $(call cc-option,-march=k8)
        cflags-$(CONFIG_MPSC) += $(call cc-option,-march=nocona)

        cflags-$(CONFIG_MCORE2) += \
                $(call cc-option,-march=core2,$(call cc-option,-mtune=generic))
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

        stackp := $(CONFIG_SHELL) $(srctree)/scripts/gcc-x86_64-has-stack-protector.sh
        stackp-$(CONFIG_CC_STACKPROTECTOR) := $(shell $(stackp) \
                "$(CC)" -fstack-protector )
        stackp-$(CONFIG_CC_STACKPROTECTOR_ALL) += $(shell $(stackp) \
                "$(CC)" -fstack-protector-all )

        KBUILD_CFLAGS += $(stackp-y)
endif

# Stackpointer is addressed different for 32 bit and 64 bit x86
sp-$(CONFIG_X86_32) := esp
sp-$(CONFIG_X86_64) := rsp

# do binutils support CFI?
cfi := $(call as-instr,.cfi_startproc\n.cfi_rel_offset $(sp-y)$(comma)0\n.cfi_endproc,-DCONFIG_AS_CFI=1)
# is .cfi_signal_frame supported too?
cfi-sigframe := $(call as-instr,.cfi_startproc\n.cfi_signal_frame\n.cfi_endproc,-DCONFIG_AS_CFI_SIGNAL_FRAME=1)
KBUILD_AFLAGS += $(cfi) $(cfi-sigframe)
KBUILD_CFLAGS += $(cfi) $(cfi-sigframe)

LDFLAGS := -m elf_$(UTS_MACHINE)

# Speed up the build
KBUILD_CFLAGS += -pipe
# Workaround for a gcc prelease that unfortunately was shipped in a suse release
KBUILD_CFLAGS += -Wno-sign-compare
#
KBUILD_CFLAGS += -fno-asynchronous-unwind-tables
# prevent gcc from generating any FP code by mistake
KBUILD_CFLAGS += $(call cc-option,-mno-sse -mno-mmx -mno-sse2 -mno-3dnow,)

###
# Sub architecture support
# fcore-y is linked before mcore-y files.

# Default subarch .c files
mcore-y  := arch/x86/mach-default/

# Voyager subarch support
mflags-$(CONFIG_X86_VOYAGER)	:= -Iinclude/asm-x86/mach-voyager
mcore-$(CONFIG_X86_VOYAGER)	:= arch/x86/mach-voyager/

# generic subarchitecture
mflags-$(CONFIG_X86_GENERICARCH):= -Iinclude/asm-x86/mach-generic
fcore-$(CONFIG_X86_GENERICARCH)	+= arch/x86/mach-generic/
mcore-$(CONFIG_X86_GENERICARCH)	:= arch/x86/mach-default/

# default subarch .h files
mflags-y += -Iinclude/asm-x86/mach-default

# 64 bit does not support subarch support - clear sub arch variables
fcore-$(CONFIG_X86_64)  :=
mcore-$(CONFIG_X86_64)  :=

KBUILD_CFLAGS += $(mflags-y)
KBUILD_AFLAGS += $(mflags-y)

###
# Kernel objects

head-y := arch/x86/kernel/head_$(BITS).o
head-y += arch/x86/kernel/head$(BITS).o
head-y += arch/x86/kernel/head.o
head-y += arch/x86/kernel/init_task.o

libs-y  += arch/x86/lib/

# Sub architecture files that needs linking first
core-y += $(fcore-y)

# Xen paravirtualization support
core-$(CONFIG_XEN) += arch/x86/xen/

# lguest paravirtualization support
core-$(CONFIG_LGUEST_GUEST) += arch/x86/lguest/

core-y += arch/x86/kernel/
core-y += arch/x86/mm/

# Remaining sub architecture files
core-y += $(mcore-y)

core-y += arch/x86/crypto/
core-y += arch/x86/vdso/
core-$(CONFIG_IA32_EMULATION) += arch/x86/ia32/

# drivers-y are linked after core-y
drivers-$(CONFIG_MATH_EMULATION) += arch/x86/math-emu/
drivers-$(CONFIG_PCI)            += arch/x86/pci/

# must be linked after kernel/
drivers-$(CONFIG_OPROFILE) += arch/x86/oprofile/

# suspend and hibernation support
drivers-$(CONFIG_PM) += arch/x86/power/

ifeq ($(CONFIG_X86_32),y)
drivers-$(CONFIG_FB) += arch/x86/video/
endif

####
# boot loader support. Several targets are kept for legacy purposes

boot := arch/x86/boot

PHONY += zImage bzImage compressed zlilo bzlilo \
         zdisk bzdisk fdimage fdimage144 fdimage288 isoimage install

# Default kernel to build
all: bzImage

# KBUILD_IMAGE specify target image being built
                    KBUILD_IMAGE := $(boot)/bzImage
zImage zlilo zdisk: KBUILD_IMAGE := $(boot)/zImage

zImage bzImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(KBUILD_IMAGE)
	$(Q)mkdir -p $(objtree)/arch/$(UTS_MACHINE)/boot
	$(Q)ln -fsn ../../x86/boot/bzImage $(objtree)/arch/$(UTS_MACHINE)/boot/$@

compressed: zImage

zlilo bzlilo: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(KBUILD_IMAGE) zlilo

zdisk bzdisk: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(KBUILD_IMAGE) zdisk

fdimage fdimage144 fdimage288 isoimage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(KBUILD_IMAGE) $@

install:
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(KBUILD_IMAGE) install

PHONY += vdso_install
vdso_install:
	$(Q)$(MAKE) $(build)=arch/x86/vdso $@

archclean:
	$(Q)rm -rf $(objtree)/arch/i386
	$(Q)rm -rf $(objtree)/arch/x86_64
	$(Q)$(MAKE) $(clean)=$(boot)

define archhelp
  echo  '* bzImage      - Compressed kernel image (arch/x86/boot/bzImage)'
  echo  '  install      - Install kernel using'
  echo  '                  (your) ~/bin/installkernel or'
  echo  '                  (distribution) /sbin/installkernel or'
  echo  '                  install to $$(INSTALL_PATH) and run lilo'
  echo  '  fdimage      - Create 1.4MB boot floppy image (arch/x86/boot/fdimage)'
  echo  '  fdimage144   - Create 1.4MB boot floppy image (arch/x86/boot/fdimage)'
  echo  '  fdimage288   - Create 2.8MB boot floppy image (arch/x86/boot/fdimage)'
  echo  '  isoimage     - Create a boot CD-ROM image (arch/x86/boot/image.iso)'
  echo  '                  bzdisk/fdimage*/isoimage also accept:'
  echo  '                  FDARGS="..."  arguments for the booted kernel'
  echo  '                  FDINITRD=file initrd for the booted kernel'
endef

CLEAN_FILES += arch/x86/boot/fdimage \
	       arch/x86/boot/image.iso \
	       arch/x86/boot/mtools.conf
