# Unified Makefile for i386 and x86_64

# select defconfig based on actual architecture
KBUILD_DEFCONFIG := $(ARCH)_defconfig

# # No need to remake these files
$(srctree)/arch/x86/Makefile%: ;

ifeq ($(ARCH),i386)
        include $(srctree)/arch/x86/Makefile_32
else
        include $(srctree)/arch/x86/Makefile_64
endif



