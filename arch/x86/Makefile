# Unified Makefile for i386 and x86_64

# No need to remake these files
$(srctree)/arch/x86/Makefile%: ;

ifeq ($(ARCH),i386)
        include $(srctree)/arch/x86/Makefile_32
else
        include $(srctree)/arch/x86/Makefile_64
endif



