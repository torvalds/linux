# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux s390-specific parts of the memory manager.
#

KCOV_INSTRUMENT := n
GCOV_PROFILE := n
UBSAN_SANITIZE := n
KASAN_SANITIZE := n

KBUILD_AFLAGS := $(KBUILD_AFLAGS_DECOMPRESSOR)
KBUILD_CFLAGS := $(KBUILD_CFLAGS_DECOMPRESSOR)

#
# Use -march=z900 for als.c to be able to print an error
# message if the kernel is started on a machine which is too old
#
ifneq ($(CC_FLAGS_MARCH),-march=z900)
AFLAGS_REMOVE_head.o		+= $(CC_FLAGS_MARCH)
AFLAGS_head.o			+= -march=z900
AFLAGS_REMOVE_mem.o		+= $(CC_FLAGS_MARCH)
AFLAGS_mem.o			+= -march=z900
CFLAGS_REMOVE_als.o		+= $(CC_FLAGS_MARCH)
CFLAGS_als.o			+= -march=z900
CFLAGS_REMOVE_sclp_early_core.o	+= $(CC_FLAGS_MARCH)
CFLAGS_sclp_early_core.o	+= -march=z900
endif

CFLAGS_sclp_early_core.o += -I$(srctree)/drivers/s390/char

obj-y	:= head.o als.o ebcdic.o sclp_early_core.o mem.o
targets	:= bzImage startup.a $(obj-y)
subdir-	:= compressed

OBJECTS := $(addprefix $(obj)/,$(obj-y))

$(obj)/bzImage: $(obj)/compressed/vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/compressed/vmlinux: $(obj)/startup.a FORCE
	$(Q)$(MAKE) $(build)=$(obj)/compressed $@

quiet_cmd_ar = AR      $@
      cmd_ar = rm -f $@; $(AR) rcsTP$(KBUILD_ARFLAGS) $@ $(filter $(OBJECTS), $^)

$(obj)/startup.a: $(OBJECTS) FORCE
	$(call if_changed,ar)

install: $(CONFIGURE) $(obj)/bzImage
	sh -x  $(srctree)/$(obj)/install.sh $(KERNELRELEASE) $(obj)/bzImage \
	      System.map "$(INSTALL_PATH)"

chkbss := $(OBJECTS)
chkbss-target := $(obj)/startup.a
include $(srctree)/arch/s390/scripts/Makefile.chkbss
