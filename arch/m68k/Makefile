#
# m68k/Makefile
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
# Copyright (C) 1994 by Hamish Macdonald
#

# override top level makefile
AS += -m68020
LDFLAGS := -m m68kelf
LDFLAGS_MODULE += -T $(srctree)/arch/m68k/kernel/module.lds
ifneq ($(SUBARCH),$(ARCH))
	ifeq ($(CROSS_COMPILE),)
		CROSS_COMPILE := $(call cc-cross-prefix, \
			m68k-linux-gnu- m68k-linux- m68k-unknown-linux-gnu-)
	endif
endif

ifdef CONFIG_SUN3
LDFLAGS_vmlinux = -N
endif

CHECKFLAGS += -D__mc68000__

# without -fno-strength-reduce the 53c7xx.c driver fails ;-(
KBUILD_CFLAGS += -pipe -fno-strength-reduce -ffixed-a2

# enable processor switch if compiled only for a single cpu
ifndef CONFIG_M68020
ifndef CONFIG_M68030

ifndef CONFIG_M68060
KBUILD_CFLAGS += -m68040
endif

ifndef CONFIG_M68040
KBUILD_CFLAGS += -m68060
endif

endif
endif

ifdef CONFIG_KGDB
# If configured for kgdb support, include debugging infos and keep the
# frame pointer
KBUILD_CFLAGS := $(subst -fomit-frame-pointer,,$(KBUILD_CFLAGS)) -g
endif

ifndef CONFIG_SUN3
head-y := arch/m68k/kernel/head.o
else
head-y := arch/m68k/kernel/sun3-head.o
endif

core-y				+= arch/m68k/kernel/	arch/m68k/mm/
libs-y				+= arch/m68k/lib/

core-$(CONFIG_Q40)		+= arch/m68k/q40/
core-$(CONFIG_AMIGA)		+= arch/m68k/amiga/
core-$(CONFIG_ATARI)		+= arch/m68k/atari/
core-$(CONFIG_MAC)		+= arch/m68k/mac/
core-$(CONFIG_HP300)		+= arch/m68k/hp300/
core-$(CONFIG_APOLLO)		+= arch/m68k/apollo/
core-$(CONFIG_MVME147)		+= arch/m68k/mvme147/
core-$(CONFIG_MVME16x)		+= arch/m68k/mvme16x/
core-$(CONFIG_BVME6000)		+= arch/m68k/bvme6000/
core-$(CONFIG_SUN3X)		+= arch/m68k/sun3x/	arch/m68k/sun3/
core-$(CONFIG_SUN3)		+= arch/m68k/sun3/	arch/m68k/sun3/prom/
core-$(CONFIG_M68040)		+= arch/m68k/fpsp040/
core-$(CONFIG_M68060)		+= arch/m68k/ifpsp060/
core-$(CONFIG_M68KFPU_EMU)	+= arch/m68k/math-emu/

all:	zImage

lilo:	vmlinux
	if [ -f $(INSTALL_PATH)/vmlinux ]; then mv -f $(INSTALL_PATH)/vmlinux $(INSTALL_PATH)/vmlinux.old; fi
	if [ -f $(INSTALL_PATH)/System.map ]; then mv -f $(INSTALL_PATH)/System.map $(INSTALL_PATH)/System.old; fi
	cat vmlinux > $(INSTALL_PATH)/vmlinux
	cp System.map $(INSTALL_PATH)/System.map
	if [ -x /sbin/lilo ]; then /sbin/lilo; else /etc/lilo/install; fi

zImage compressed: vmlinux.gz

vmlinux.gz: vmlinux

ifndef CONFIG_KGDB
	cp vmlinux vmlinux.tmp
	$(STRIP) vmlinux.tmp
	gzip -9c vmlinux.tmp >vmlinux.gz
	rm vmlinux.tmp
else
	gzip -9c vmlinux >vmlinux.gz
endif

bzImage: vmlinux.bz2

vmlinux.bz2: vmlinux

ifndef CONFIG_KGDB
	cp vmlinux vmlinux.tmp
	$(STRIP) vmlinux.tmp
	bzip2 -1c vmlinux.tmp >vmlinux.bz2
	rm vmlinux.tmp
else
	bzip2 -1c vmlinux >vmlinux.bz2
endif

archclean:
	rm -f vmlinux.gz vmlinux.bz2
