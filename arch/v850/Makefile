#
# arch/v850/Makefile
#
#  Copyright (C) 2001,02,03  NEC Corporation
#  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

arch_dir = arch/v850

CFLAGS += -mv850e
# r16 is a fixed pointer to the current task
CFLAGS += -ffixed-r16 -mno-prolog-function
CFLAGS += -fno-builtin
CFLAGS += -D__linux__ -DUTS_SYSNAME=\"uClinux\"

# This prevents the linker from consolidating the .gnu.linkonce.this_module
# section into .text (which the v850 default linker script for -r does for
# some reason)
LDFLAGS_MODULE += --unique=.gnu.linkonce.this_module

OBJCOPY_FLAGS_BLOB := -I binary -O elf32-little -B v850e


head-y := $(arch_dir)/kernel/head.o $(arch_dir)/kernel/init_task.o
core-y += $(arch_dir)/kernel/
libs-y += $(arch_dir)/lib/


# Deal with the initial contents of the root device
ifdef ROOT_FS_IMAGE
core-y += root_fs_image.o

# Because the kernel build-system erases all explicit .o build rules, we
# have to use an intermediate target to fool it into building for us.
# This results in it being built anew each time, but that's alright.
root_fs_image.o: root_fs_image_force

root_fs_image_force: $(ROOT_FS_IMAGE)
	$(OBJCOPY) $(OBJCOPY_FLAGS_BLOB) --rename-section .data=.root,alloc,load,readonly,data,contents $< root_fs_image.o
endif


prepare: include/asm-$(ARCH)/asm-consts.h

# Generate constants from C code for use by asm files
arch/$(ARCH)/kernel/asm-consts.s: include/asm include/linux/version.h \
				   include/config/MARKER

include/asm-$(ARCH)/asm-consts.h: arch/$(ARCH)/kernel/asm-consts.s
	$(call filechk,gen-asm-offsets)

CLEAN_FILES += include/asm-$(ARCH)/asm-consts.h \
	       arch/$(ARCH)/kernel/asm-consts.s \
	       root_fs_image.o
