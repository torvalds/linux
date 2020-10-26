#
# ia64/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1998-2004 by David Mosberger-Tang <davidm@hpl.hp.com>
#

KBUILD_DEFCONFIG := generic_defconfig

NM := $(CROSS_COMPILE)nm -B
READELF := $(CROSS_COMPILE)readelf

CHECKFLAGS	+= -D__ia64=1 -D__ia64__=1 -D_LP64 -D__LP64__

OBJCOPYFLAGS	:= --strip-all
LDFLAGS_vmlinux	:= -static
KBUILD_AFLAGS_KERNEL := -mconstant-gp
EXTRA		:=

cflags-y	:= -pipe $(EXTRA) -ffixed-r13 -mfixed-range=f12-f15,f32-f127 \
		   -falign-functions=32 -frename-registers -fno-optimize-sibling-calls
KBUILD_CFLAGS_KERNEL := -mconstant-gp

GAS_STATUS	= $(shell $(srctree)/arch/ia64/scripts/check-gas "$(CC)" "$(OBJDUMP)")
KBUILD_CPPFLAGS += $(shell $(srctree)/arch/ia64/scripts/toolchain-flags "$(CC)" "$(OBJDUMP)" "$(READELF)")

ifeq ($(GAS_STATUS),buggy)
$(error Sorry, you need a newer version of the assember, one that is built from	\
	a source-tree that post-dates 18-Dec-2002.  You can find a pre-compiled	\
	static binary of such an assembler at:					\
										\
		ftp://ftp.hpl.hp.com/pub/linux-ia64/gas-030124.tar.gz)
endif

quiet_cmd_gzip = GZIP    $@
cmd_gzip = cat $(real-prereqs) | $(KGZIP) -n -f -9 > $@

quiet_cmd_objcopy = OBJCOPY $@
cmd_objcopy = $(OBJCOPY) $(OBJCOPYFLAGS) $(OBJCOPYFLAGS_$(@F)) $< $@

KBUILD_CFLAGS += $(cflags-y)
head-y := arch/ia64/kernel/head.o

libs-y				+= arch/ia64/lib/
core-y				+= arch/ia64/kernel/ arch/ia64/mm/
core-$(CONFIG_IA64_SGI_UV)	+= arch/ia64/uv/

drivers-y			+= arch/ia64/pci/ arch/ia64/hp/common/
drivers-$(CONFIG_OPROFILE)	+= arch/ia64/oprofile/

PHONY += compressed check

all: compressed unwcheck

compressed: vmlinux.gz

vmlinuz: vmlinux.gz

vmlinux.gz: vmlinux.bin FORCE
	$(call if_changed,gzip)

vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

unwcheck: vmlinux
	-$(Q)READELF=$(READELF) $(PYTHON) $(srctree)/arch/ia64/scripts/unwcheck.py $<

archclean:

archheaders:
	$(Q)$(MAKE) $(build)=arch/ia64/kernel/syscalls all

CLEAN_FILES += vmlinux.gz

install: vmlinux.gz
	sh $(srctree)/arch/ia64/install.sh $(KERNELRELEASE) $< System.map "$(INSTALL_PATH)"

define archhelp
  echo '* compressed	- Build compressed kernel image'
  echo '  install	- Install compressed kernel image'
  echo '* unwcheck	- Check vmlinux for invalid unwind info'
endef

archprepare: make_nr_irqs_h
PHONY += make_nr_irqs_h

make_nr_irqs_h:
	$(Q)$(MAKE) $(build)=arch/ia64/kernel include/generated/nr-irqs.h
