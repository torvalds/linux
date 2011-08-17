KBUILD_DEFCONFIG := multi_defconfig

ifdef CONFIG_MMU
include $(srctree)/arch/m68k/Makefile_mm
else
include $(srctree)/arch/m68k/Makefile_no
endif
