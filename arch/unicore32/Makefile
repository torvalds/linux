#
# arch/unicore32/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2002~2010 by Guan Xue-tao
#
ifneq ($(SUBARCH),$(ARCH))
	ifeq ($(CROSS_COMPILE),)
		CROSS_COMPILE := $(call cc-cross-prefix, unicore32-linux-)
	endif
endif

LDFLAGS_vmlinux		:= -p --no-undefined -X

OBJCOPYFLAGS		:= -O binary -R .note -R .note.gnu.build-id -R .comment -S

# Never generate .eh_frame
KBUILD_CFLAGS		+= $(call cc-option,-fno-dwarf2-cfi-asm)

# Never use hard float in kernel
KBUILD_CFLAGS		+= -msoft-float

ifeq ($(CONFIG_FRAME_POINTER),y)
KBUILD_CFLAGS		+= -mno-sched-prolog
endif

CHECKFLAGS		+= -D__unicore32__

head-y			:= arch/unicore32/kernel/head.o
head-y			+= arch/unicore32/kernel/init_task.o

core-y			+= arch/unicore32/kernel/
core-y			+= arch/unicore32/mm/

libs-y			+= arch/unicore32/lib/

boot			:= arch/unicore32/boot

# Default defconfig and target when executing plain make
KBUILD_DEFCONFIG	:= $(ARCH)_defconfig
KBUILD_IMAGE		:= zImage

all:	$(KBUILD_IMAGE)

zImage Image uImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

define archhelp
  echo  '* zImage        - Compressed kernel image (arch/$(ARCH)/boot/zImage)'
  echo  '  Image         - Uncompressed kernel image (arch/$(ARCH)/boot/Image)'
  echo  '  uImage        - U-Boot wrapped zImage'
endef
