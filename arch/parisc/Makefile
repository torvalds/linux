#
# parisc/Makefile
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
# Copyright (C) 1994 by Linus Torvalds
# Portions Copyright (C) 1999 The Puffin Group
#
# Modified for PA-RISC Linux by Paul Lahaie, Alex deVries, 
# Mike Shaver, Helge Deller and Martin K. Petersen
#
NM		= sh $(srctree)/arch/parisc/nm
CHECKFLAGS	+= -D__hppa__=1

MACHINE		:= $(shell uname -m)
ifeq ($(MACHINE),parisc*)
NATIVE		:= 1
endif

ifdef CONFIG_64BIT
UTS_MACHINE	:= parisc64
CHECKFLAGS	+= -D__LP64__=1 -m64
WIDTH		:= 64
CROSS_COMPILE	:= hppa64-linux-gnu-
else # 32-bit
WIDTH		:=
endif

# attempt to help out folks who are cross-compiling
ifeq ($(NATIVE),1)
CROSS_COMPILE	:= hppa$(WIDTH)-linux-
endif

OBJCOPY_FLAGS =-O binary -R .note -R .comment -S

cflags-y	:= -pipe

# These flags should be implied by an hppa-linux configuration, but they
# are not in gcc 3.2.
cflags-y	+= -mno-space-regs -mfast-indirect-calls

# Currently we save and restore fpregs on all kernel entry/interruption paths.
# If that gets optimized, we might need to disable the use of fpregs in the
# kernel.
cflags-y	+= -mdisable-fpregs

# Without this, "ld -r" results in .text sections that are too big
# (> 0x40000) for branches to reach stubs.
cflags-y	+= -ffunction-sections

# select which processor to optimise for
cflags-$(CONFIG_PA7100)		+= -march=1.1 -mschedule=7100
cflags-$(CONFIG_PA7200)		+= -march=1.1 -mschedule=7200
cflags-$(CONFIG_PA7100LC)	+= -march=1.1 -mschedule=7100LC
cflags-$(CONFIG_PA7300LC)	+= -march=1.1 -mschedule=7300
cflags-$(CONFIG_PA8X00)		+= -march=2.0 -mschedule=8000

head-y			:= arch/parisc/kernel/head.o 

KBUILD_CFLAGS	+= $(cflags-y)

kernel-y			:= mm/ kernel/ math-emu/ kernel/init_task.o
kernel-$(CONFIG_HPUX)		+= hpux/

core-y	+= $(addprefix arch/parisc/, $(kernel-y))
libs-y	+= arch/parisc/lib/ `$(CC) -print-libgcc-file-name`

drivers-$(CONFIG_OPROFILE)		+= arch/parisc/oprofile/

PALO := $(shell if which palo; then : ; \
	elif [ -x /sbin/palo ]; then echo /sbin/palo; \
	fi)

PALOCONF := $(shell if [ -f $(src)/palo.conf ]; then echo $(src)/palo.conf; \
	else echo $(obj)/palo.conf; \
	fi)

palo: vmlinux
	@if test ! -x "$(PALO)"; then \
		echo 'ERROR: Please install palo first (apt-get install palo)';\
		echo 'or build it from source and install it somewhere in your $$PATH';\
		false; \
	fi
	@if test ! -f "$(PALOCONF)"; then \
		cp $(src)/arch/parisc/defpalo.conf $(obj)/palo.conf; \
		echo 'A generic palo config file ($(obj)/palo.conf) has been created for you.'; \
		echo 'You should check it and re-run "make palo".'; \
		echo 'WARNING: the "lifimage" file is now placed in this directory by default!'; \
		false; \
	fi
	$(PALO) -f $(PALOCONF)

# Shorthands for known targets not supported by parisc, use vmlinux as default
Image zImage bzImage: vmlinux

kernel_install: vmlinux
	sh $(src)/arch/parisc/install.sh \
			$(KERNELRELEASE) $< System.map "$(INSTALL_PATH)"

install: kernel_install modules_install

CLEAN_FILES	+= lifimage
MRPROPER_FILES	+= palo.conf

define archhelp
	@echo  '* vmlinux	- Uncompressed kernel image (./vmlinux)'
	@echo  '  palo		- Bootable image (./lifimage)'
	@echo  '  install	- Install kernel using'
	@echo  '		  (your) ~/bin/installkernel or'
	@echo  '		  (distribution) /sbin/installkernel or'
	@echo  '		  copy to $$(INSTALL_PATH)'
endef

# we require gcc 3.3 or above to compile the kernel
archprepare: checkbin
checkbin:
	@if test "$(call cc-version)" -lt "0303"; then \
		echo -n "Sorry, GCC v3.3 or above is required to build " ; \
		echo "the kernel." ; \
		false ; \
	fi
