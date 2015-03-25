# 64-bit Android AArch64 compiler
TARGET_PRIMARY_ARCH := target_aarch64
ifeq ($(MULTIARCH),1)
 TARGET_SECONDARY_ARCH := target_armv7-a
 ifneq ($(MAKECMDGOALS),kbuild)
  ifneq ($(COMPONENTS),)
   ifeq ($(CROSS_COMPILE_SECONDARY),)
    $(error CROSS_COMPILE_SECONDARY must be set for multiarch ARM builds)
   endif
  endif
 endif
endif
