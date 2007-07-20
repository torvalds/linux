#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
# Licensed under the GPL
#

ARCH_DIR := arch/um
OS := $(shell uname -s)
# We require bash because the vmlinux link and loader script cpp use bash
# features.
SHELL := /bin/bash

filechk_gen_header = $<

core-y			+= $(ARCH_DIR)/kernel/		\
			   $(ARCH_DIR)/drivers/		\
			   $(ARCH_DIR)/os-$(OS)/

# Have to precede the include because the included Makefiles reference them.
SYMLINK_HEADERS := archparam.h system.h sigcontext.h processor.h ptrace.h \
	module.h vm-flags.h elf.h host_ldt.h
SYMLINK_HEADERS := $(foreach header,$(SYMLINK_HEADERS),include/asm-um/$(header))

# XXX: The "os" symlink is only used by arch/um/include/os.h, which includes
# ../os/include/file.h
#
# These are cleaned up during mrproper. Please DO NOT fix it again, this is
# the Correct Thing(tm) to do!
ARCH_SYMLINKS = include/asm-um/arch $(ARCH_DIR)/include/sysdep $(ARCH_DIR)/os \
	$(SYMLINK_HEADERS) $(ARCH_DIR)/include/uml-config.h

um-modes-$(CONFIG_MODE_TT) += tt
um-modes-$(CONFIG_MODE_SKAS) += skas

MODE_INCLUDE	+= $(foreach mode,$(um-modes-y),\
		   -I$(srctree)/$(ARCH_DIR)/include/$(mode))

MAKEFILES-INCL	+= $(foreach mode,$(um-modes-y),\
		   $(srctree)/$(ARCH_DIR)/Makefile-$(mode))

ifneq ($(MAKEFILES-INCL),)
  include $(MAKEFILES-INCL)
endif

ARCH_INCLUDE	:= -I$(ARCH_DIR)/include
ifneq ($(KBUILD_SRC),)
ARCH_INCLUDE	+= -I$(srctree)/$(ARCH_DIR)/include
endif
SYS_DIR		:= $(ARCH_DIR)/include/sysdep-$(SUBARCH)

# -Dvmap=kernel_vmap prevents anything from referencing the libpcap.o symbol so
# named - it's a common symbol in libpcap, so we get a binary which crashes.
#
# Same things for in6addr_loopback and mktime - found in libc. For these two we
# only get link-time error, luckily.
#
# These apply to USER_CFLAGS to.

CFLAGS += $(CFLAGS-y) -D__arch_um__ -DSUBARCH=\"$(SUBARCH)\"	\
	$(ARCH_INCLUDE) $(MODE_INCLUDE) -Dvmap=kernel_vmap	\
	-Din6addr_loopback=kernel_in6addr_loopback

AFLAGS += $(ARCH_INCLUDE)

USER_CFLAGS = $(patsubst $(KERNEL_DEFINES),,$(patsubst -D__KERNEL__,,\
	$(patsubst -I%,,$(CFLAGS)))) $(ARCH_INCLUDE) $(MODE_INCLUDE) \
	-D_FILE_OFFSET_BITS=64

include $(srctree)/$(ARCH_DIR)/Makefile-$(SUBARCH)

#This will adjust *FLAGS accordingly to the platform.
include $(srctree)/$(ARCH_DIR)/Makefile-os-$(OS)

# -Derrno=kernel_errno - This turns all kernel references to errno into
# kernel_errno to separate them from the libc errno.  This allows -fno-common
# in CFLAGS.  Otherwise, it would cause ld to complain about the two different
# errnos.
# These apply to kernelspace only.

KERNEL_DEFINES = -Derrno=kernel_errno -Dsigprocmask=kernel_sigprocmask \
	-Dmktime=kernel_mktime $(ARCH_KERNEL_DEFINES)
CFLAGS += $(KERNEL_DEFINES)
CFLAGS += $(call cc-option,-fno-unit-at-a-time,)

# These are needed for clean and mrproper, since in that case .config is not
# included; the values here are meaningless

CONFIG_NEST_LEVEL ?= 0
CONFIG_KERNEL_HALF_GIGS ?= 0

SIZE = (($(CONFIG_NEST_LEVEL) + $(CONFIG_KERNEL_HALF_GIGS)) * 0x20000000)

PHONY += linux

all: linux

linux: vmlinux
	@echo '  LINK $@'
	$(Q)ln -f $< $@

define archhelp
  echo '* linux		- Binary kernel image (./linux) - for backward'
  echo '		   compatibility only, this creates a hard link to the'
  echo '		   real kernel binary, the "vmlinux" binary you'
  echo '		   find in the kernel root.'
endef

ifneq ($(KBUILD_SRC),)
$(shell mkdir -p $(ARCH_DIR) && ln -fsn $(srctree)/$(ARCH_DIR)/Kconfig.$(SUBARCH) $(ARCH_DIR)/Kconfig.arch)
else
$(shell cd $(ARCH_DIR) && ln -sf Kconfig.$(SUBARCH) Kconfig.arch)
endif

archprepare: $(ARCH_SYMLINKS) $(ARCH_DIR)/include/user_constants.h
prepare: $(ARCH_DIR)/include/kern_constants.h

LINK-$(CONFIG_LD_SCRIPT_STATIC) += -static
LINK-$(CONFIG_LD_SCRIPT_DYN) += -Wl,-rpath,/lib

CFLAGS_NO_HARDENING := $(call cc-option, -fno-PIC,) $(call cc-option, -fno-pic,) \
	$(call cc-option, -fno-stack-protector,) \
	$(call cc-option, -fno-stack-protector-all,)

CPP_MODE-$(CONFIG_MODE_TT) := -DMODE_TT
CONFIG_KERNEL_STACK_ORDER ?= 2
STACK_SIZE := $(shell echo $$[ 4096 * (1 << $(CONFIG_KERNEL_STACK_ORDER)) ] )

ifndef START
  START = $(shell echo $$[ $(TOP_ADDR) - $(SIZE) ] )
endif

CPPFLAGS_vmlinux.lds = -U$(SUBARCH) \
	-DSTART=$(START) -DELF_ARCH=$(ELF_ARCH) \
	-DELF_FORMAT="$(ELF_FORMAT)" $(CPP_MODE-y) \
	-DKERNEL_STACK_SIZE=$(STACK_SIZE) \
	-DUNMAP_PATH=arch/um/sys-$(SUBARCH)/unmap.o

#The wrappers will select whether using "malloc" or the kernel allocator.
LINK_WRAPS = -Wl,--wrap,malloc -Wl,--wrap,free -Wl,--wrap,calloc

CFLAGS_vmlinux := $(LINK-y) $(LINK_WRAPS)
define cmd_vmlinux__
	$(CC) $(CFLAGS_vmlinux) -o $@ \
	-Wl,-T,$(vmlinux-lds) $(vmlinux-init) \
	-Wl,--start-group $(vmlinux-main) -Wl,--end-group \
	-lutil \
	$(filter-out $(vmlinux-lds) $(vmlinux-init) $(vmlinux-main) vmlinux.o \
	FORCE ,$^) ; rm -f linux
endef

#When cleaning we don't include .config, so we don't include
#TT or skas makefiles and don't clean skas_ptregs.h.
CLEAN_FILES += linux x.i gmon.out $(ARCH_DIR)/include/uml-config.h \
	$(ARCH_DIR)/include/user_constants.h \
	$(ARCH_DIR)/include/kern_constants.h $(ARCH_DIR)/Kconfig.arch

MRPROPER_FILES += $(ARCH_SYMLINKS)

archclean:
	@find . \( -name '*.bb' -o -name '*.bbg' -o -name '*.da' \
		-o -name '*.gcov' \) -type f -print | xargs rm -f

$(SYMLINK_HEADERS):
	@echo '  SYMLINK $@'
ifneq ($(KBUILD_SRC),)
	$(Q)mkdir -p $(objtree)/include/asm-um
	$(Q)ln -fsn $(srctree)/include/asm-um/$(basename $(notdir $@))-$(SUBARCH)$(suffix $@) $@
else
	$(Q)cd $(TOPDIR)/$(dir $@) ; \
	ln -sf $(basename $(notdir $@))-$(SUBARCH)$(suffix $@) $(notdir $@)
endif

include/asm-um/arch:
	@echo '  SYMLINK $@'
ifneq ($(KBUILD_SRC),)
	$(Q)mkdir -p $(objtree)/include/asm-um
	$(Q)ln -fsn $(srctree)/include/asm-$(SUBARCH) include/asm-um/arch
else
	$(Q)cd $(TOPDIR)/include/asm-um && ln -sf ../asm-$(SUBARCH) arch
endif

$(objtree)/$(ARCH_DIR)/include:
	@echo '  MKDIR $@'
	$(Q)mkdir -p $@

$(ARCH_DIR)/include/sysdep: $(objtree)/$(ARCH_DIR)/include
	@echo '  SYMLINK $@'
ifneq ($(KBUILD_SRC),)
	$(Q)ln -fsn $(srctree)/$(ARCH_DIR)/include/sysdep-$(SUBARCH) $(ARCH_DIR)/include/sysdep
else
	$(Q)cd $(ARCH_DIR)/include && ln -sf sysdep-$(SUBARCH) sysdep
endif

$(ARCH_DIR)/os:
	@echo '  SYMLINK $@'
ifneq ($(KBUILD_SRC),)
	$(Q)ln -fsn $(srctree)/$(ARCH_DIR)/os-$(OS) $(ARCH_DIR)/os
else
	$(Q)cd $(ARCH_DIR) && ln -sf os-$(OS) os
endif

# Generated files
define filechk_umlconfig
	sed 's/ CONFIG/ UML_CONFIG/'
endef

$(ARCH_DIR)/include/uml-config.h : include/linux/autoconf.h
	$(call filechk,umlconfig)

$(ARCH_DIR)/sys-$(SUBARCH)/user-offsets.s: FORCE
	$(Q)$(MAKE) $(build)=$(ARCH_DIR)/sys-$(SUBARCH) $@

define filechk_gen-asm-offsets
        (set -e; \
         echo "/*"; \
         echo " * DO NOT MODIFY."; \
         echo " *"; \
         echo " * This file was generated by arch/$(ARCH)/Makefile"; \
         echo " *"; \
         echo " */"; \
         echo ""; \
         sed -ne "/^->/{s:^->\([^ ]*\) [\$$#]*\([^ ]*\) \(.*\):#define \1 \2 /* \3 */:; s:->::; p;}"; \
         echo ""; )
endef

$(ARCH_DIR)/include/user_constants.h: $(ARCH_DIR)/sys-$(SUBARCH)/user-offsets.s
	$(call filechk,gen-asm-offsets)

$(ARCH_DIR)/include/kern_constants.h: $(objtree)/$(ARCH_DIR)/include
	@echo '  SYMLINK $@'
	$(Q)ln -sf ../../../include/asm-um/asm-offsets.h $@

export SUBARCH USER_CFLAGS CFLAGS_NO_HARDENING OS
