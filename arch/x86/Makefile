# Unified Makefile for i386 and x86_64

# select defconfig based on actual architecture
ifeq ($(ARCH),x86)
        KBUILD_DEFCONFIG := i386_defconfig
else
        KBUILD_DEFCONFIG := $(ARCH)_defconfig
endif

# No need to remake these files
$(srctree)/arch/x86/Makefile%: ;

ifeq ($(CONFIG_X86_32),y)
        UTS_MACHINE := i386
        include $(srctree)/arch/x86/Makefile_32
else
        UTS_MACHINE := x86_64
        include $(srctree)/arch/x86/Makefile_64
endif
