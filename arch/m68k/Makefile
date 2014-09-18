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
# Copyright (C) 2002,2011 Greg Ungerer <gerg@snapgear.com>
#

KBUILD_DEFCONFIG := multi_defconfig

ifneq ($(SUBARCH),$(ARCH))
	ifeq ($(CROSS_COMPILE),)
		CROSS_COMPILE := $(call cc-cross-prefix, \
			m68k-linux-gnu- m68k-linux- m68k-unknown-linux-gnu-)
	endif
endif

#
#	Enable processor type. Ordering of these is important - we want to
#	use the minimum processor type of the range we support. The logic
#	for 680x0 will only allow use of the -m68060 or -m68040 if no other
#	680x0 type is specified - and no option is specified for 68030 or
#	68020. The other m68k/ColdFire types always specify some type of
#	compiler cpu type flag.
#
ifndef CONFIG_M68040
cpuflags-$(CONFIG_M68060)	:= -m68060
endif
ifndef CONFIG_M68060
cpuflags-$(CONFIG_M68040)	:= -m68040
endif
cpuflags-$(CONFIG_M68030)	:=
cpuflags-$(CONFIG_M68020)	:=
cpuflags-$(CONFIG_M68360)	:= -m68332
cpuflags-$(CONFIG_M68000)	:= -m68000
cpuflags-$(CONFIG_M5441x)	:= $(call cc-option,-mcpu=54455,-mcfv4e)
cpuflags-$(CONFIG_M54xx)	:= $(call cc-option,-mcpu=5475,-m5200)
cpuflags-$(CONFIG_M5407)	:= $(call cc-option,-mcpu=5407,-m5200)
cpuflags-$(CONFIG_M532x)	:= $(call cc-option,-mcpu=532x,-m5307)
cpuflags-$(CONFIG_M537x)	:= $(call cc-option,-mcpu=537x,-m5307)
cpuflags-$(CONFIG_M5307)	:= $(call cc-option,-mcpu=5307,-m5200)
cpuflags-$(CONFIG_M528x)	:= $(call cc-option,-mcpu=528x,-m5307)
cpuflags-$(CONFIG_M5275)	:= $(call cc-option,-mcpu=5275,-m5307)
cpuflags-$(CONFIG_M5272)	:= $(call cc-option,-mcpu=5272,-m5307)
cpuflags-$(CONFIG_M5271)	:= $(call cc-option,-mcpu=5271,-m5307)
cpuflags-$(CONFIG_M523x)	:= $(call cc-option,-mcpu=523x,-m5307)
cpuflags-$(CONFIG_M525x)	:= $(call cc-option,-mcpu=5253,-m5200)
cpuflags-$(CONFIG_M5249)	:= $(call cc-option,-mcpu=5249,-m5200)
cpuflags-$(CONFIG_M520x)	:= $(call cc-option,-mcpu=5208,-m5200)
cpuflags-$(CONFIG_M5206e)	:= $(call cc-option,-mcpu=5206e,-m5200)
cpuflags-$(CONFIG_M5206)	:= $(call cc-option,-mcpu=5206,-m5200)

KBUILD_AFLAGS += $(cpuflags-y)
KBUILD_CFLAGS += $(cpuflags-y) -pipe
ifdef CONFIG_MMU
# without -fno-strength-reduce the 53c7xx.c driver fails ;-(
KBUILD_CFLAGS += -fno-strength-reduce -ffixed-a2
else
# we can use a m68k-linux-gcc toolchain with these in place
KBUILD_CFLAGS += -DUTS_SYSNAME=\"uClinux\"
KBUILD_CFLAGS += -D__uClinux__
KBUILD_AFLAGS += -D__uClinux__
endif

LDFLAGS := -m m68kelf
KBUILD_LDFLAGS_MODULE += -T $(srctree)/arch/m68k/kernel/module.lds

ifdef CONFIG_SUN3
LDFLAGS_vmlinux = -N
endif

CHECKFLAGS += -D__mc68000__


ifdef CONFIG_KGDB
# If configured for kgdb support, include debugging infos and keep the
# frame pointer
KBUILD_CFLAGS := $(subst -fomit-frame-pointer,,$(KBUILD_CFLAGS)) -g
endif

#
# Select the assembler head startup code. Order is important. The default
# head code is first, processor specific selections can override it after.
#
head-y				:= arch/m68k/kernel/head.o
head-$(CONFIG_SUN3)		:= arch/m68k/kernel/sun3-head.o
head-$(CONFIG_M68360)		:= arch/m68k/platform/68360/head.o
head-$(CONFIG_M68000)		:= arch/m68k/platform/68000/head.o
head-$(CONFIG_COLDFIRE)		:= arch/m68k/platform/coldfire/head.o

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
core-$(CONFIG_NATFEAT)		+= arch/m68k/emu/
core-$(CONFIG_M68040)		+= arch/m68k/fpsp040/
core-$(CONFIG_M68060)		+= arch/m68k/ifpsp060/
core-$(CONFIG_M68KFPU_EMU)	+= arch/m68k/math-emu/
core-$(CONFIG_M68360)		+= arch/m68k/platform/68360/
core-$(CONFIG_M68000)		+= arch/m68k/platform/68000/
core-$(CONFIG_COLDFIRE)		+= arch/m68k/platform/coldfire/


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

install:
	sh $(srctree)/arch/m68k/install.sh $(KERNELRELEASE) vmlinux.gz System.map "$(INSTALL_PATH)"
