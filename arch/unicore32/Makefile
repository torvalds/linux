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

ASM_GENERATED_DIR	:= $(srctree)/arch/unicore32/include/generated
LINUXINCLUDE		+= -I$(ASM_GENERATED_DIR)

ASM_GENERIC_HEADERS	:= atomic.h auxvec.h
ASM_GENERIC_HEADERS	+= bitsperlong.h bug.h bugs.h
ASM_GENERIC_HEADERS	+= cputime.h current.h
ASM_GENERIC_HEADERS	+= device.h div64.h
ASM_GENERIC_HEADERS	+= emergency-restart.h errno.h
ASM_GENERIC_HEADERS	+= fb.h fcntl.h ftrace.h futex.h
ASM_GENERIC_HEADERS	+= hardirq.h hw_irq.h
ASM_GENERIC_HEADERS	+= ioctl.h ioctls.h ipcbuf.h irq_regs.h
ASM_GENERIC_HEADERS	+= kdebug.h kmap_types.h
ASM_GENERIC_HEADERS	+= local.h
ASM_GENERIC_HEADERS	+= mman.h module.h msgbuf.h
ASM_GENERIC_HEADERS	+= param.h parport.h percpu.h poll.h posix_types.h
ASM_GENERIC_HEADERS	+= resource.h
ASM_GENERIC_HEADERS	+= scatterlist.h sections.h segment.h sembuf.h serial.h
ASM_GENERIC_HEADERS	+= setup.h shmbuf.h shmparam.h
ASM_GENERIC_HEADERS	+= siginfo.h signal.h sizes.h
ASM_GENERIC_HEADERS	+= socket.h sockios.h stat.h statfs.h swab.h syscalls.h
ASM_GENERIC_HEADERS	+= termbits.h termios.h topology.h types.h
ASM_GENERIC_HEADERS	+= ucontext.h unaligned.h user.h
ASM_GENERIC_HEADERS	+= vga.h
ASM_GENERIC_HEADERS	+= xor.h

archprepare:
ifneq ($(ASM_GENERATED_DIR), $(wildcard $(ASM_GENERATED_DIR)))
	$(Q)mkdir -p $(ASM_GENERATED_DIR)/asm
	$(Q)$(foreach a, $(ASM_GENERIC_HEADERS),	\
		echo '#include <asm-generic/$a>'	\
			> $(ASM_GENERATED_DIR)/asm/$a; )
endif

boot			:= arch/unicore32/boot

# Default target when executing plain make
KBUILD_IMAGE		:= zImage

all:	$(KBUILD_IMAGE)

zImage Image uImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

MRPROPER_DIRS		+= $(ASM_GENERATED_DIR)

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

define archhelp
  echo  '* zImage        - Compressed kernel image (arch/$(ARCH)/boot/zImage)'
  echo  '  Image         - Uncompressed kernel image (arch/$(ARCH)/boot/Image)'
  echo  '  uImage        - U-Boot wrapped zImage'
endef
