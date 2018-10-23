LDFLAGS_vmlinux	:= --no-undefined -X
OBJCOPYFLAGS	:= -O binary -R .note -R .note.gnu.build-id -R .comment -S

KBUILD_DEFCONFIG := defconfig

comma = ,

ifdef CONFIG_FUNCTION_TRACER
arch-y += -malways-save-lp -mno-relax
endif

KBUILD_CFLAGS	+= $(call cc-option, -mno-sched-prolog-epilog)
KBUILD_CFLAGS	+= -mcmodel=large

KBUILD_CFLAGS	+=$(arch-y) $(tune-y)
KBUILD_AFLAGS	+=$(arch-y) $(tune-y)

#Default value
head-y		 := arch/nds32/kernel/head.o
textaddr-y	 := $(CONFIG_PAGE_OFFSET)+0xc000

TEXTADDR := $(textaddr-y)

export	TEXTADDR


# If we have a machine-specific directory, then include it in the build.
core-y				+= arch/nds32/kernel/ arch/nds32/mm/
libs-y				+= arch/nds32/lib/

ifneq '$(CONFIG_NDS32_BUILTIN_DTB)' '""'
BUILTIN_DTB := y
else
BUILTIN_DTB := n
endif

ifdef CONFIG_CPU_LITTLE_ENDIAN
KBUILD_CFLAGS   += $(call cc-option, -EL)
KBUILD_AFLAGS   += $(call cc-option, -EL)
KBUILD_LDFLAGS  += $(call cc-option, -EL)
CHECKFLAGS      += -D__NDS32_EL__
else
KBUILD_CFLAGS   += $(call cc-option, -EB)
KBUILD_AFLAGS   += $(call cc-option, -EB)
KBUILD_LDFLAGS  += $(call cc-option, -EB)
CHECKFLAGS      += -D__NDS32_EB__
endif

boot := arch/nds32/boot
core-$(BUILTIN_DTB) += $(boot)/dts/

.PHONY: FORCE

Image: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@


PHONY += vdso_install
vdso_install:
	$(Q)$(MAKE) $(build)=arch/nds32/kernel/vdso $@

prepare: vdso_prepare
vdso_prepare: prepare0
	$(Q)$(MAKE) $(build)=arch/nds32/kernel/vdso include/generated/vdso-offsets.h

CLEAN_FILES += include/asm-nds32/constants.h*

# We use MRPROPER_FILES and CLEAN_FILES now
archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

define archhelp
  echo  '  Image         - kernel image (arch/$(ARCH)/boot/Image)'
endef
