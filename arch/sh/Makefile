# $Id: Makefile,v 1.35 2004/04/15 03:39:20 sugioka Exp $
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1999  Kaz Kojima
# Copyright (C) 2002, 2003, 2004  Paul Mundt
# Copyright (C) 2002  M. R. Brown
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture
#
isa-y					:= any
isa-$(CONFIG_SH_DSP)			:= sh
isa-$(CONFIG_CPU_SH2)			:= sh2
isa-$(CONFIG_CPU_SH2A)			:= sh2a
isa-$(CONFIG_CPU_SH3)			:= sh3
isa-$(CONFIG_CPU_SH4)			:= sh4
isa-$(CONFIG_CPU_SH4A)			:= sh4a
isa-$(CONFIG_CPU_SH4AL_DSP)		:= sh4al

isa-$(CONFIG_SH_DSP)			:= $(isa-y)-dsp

ifndef CONFIG_MMU
isa-y			:= $(isa-y)-nommu
endif

ifndef CONFIG_SH_DSP
ifndef CONFIG_SH_FPU
isa-y			:= $(isa-y)-nofpu
endif
endif

cflags-$(CONFIG_CPU_SH2)		:= -m2
cflags-$(CONFIG_CPU_SH3)		:= -m3
cflags-$(CONFIG_CPU_SH4)		:= -m4 \
	$(call cc-option,-mno-implicit-fp,-m4-nofpu)
cflags-$(CONFIG_CPU_SH4A)		:= -m4a $(call cc-option,-m4a-nofpu,)

cflags-$(CONFIG_CPU_BIG_ENDIAN)		+= -mb
cflags-$(CONFIG_CPU_LITTLE_ENDIAN)	+= -ml

cflags-y	+= $(call as-option,-Wa$(comma)-isa=$(isa-y),) -ffreestanding

cflags-$(CONFIG_SH_DSP)			+= -Wa,-dsp
cflags-$(CONFIG_SH_KGDB)		+= -g

cflags-$(CONFIG_MORE_COMPILE_OPTIONS)	+= \
	$(shell echo $(CONFIG_COMPILE_OPTIONS) | sed -e 's/"//g')

OBJCOPYFLAGS	:= -O binary -R .note -R .comment -R .stab -R .stabstr -S

#
# arch/sh/defconfig doesn't reflect any real hardware, and as such should
# never be used by anyone. Use a board-specific defconfig that has a
# reasonable chance of being current instead.
#
KBUILD_DEFCONFIG := rts7751r2d_defconfig

#
# Choosing incompatible machines durings configuration will result in
# error messages during linking.
#
LDFLAGS_vmlinux     += -e _stext

ifdef CONFIG_CPU_LITTLE_ENDIAN
LDFLAGS_vmlinux		+= --defsym 'jiffies=jiffies_64'
LDFLAGS			+= -EL
else
LDFLAGS_vmlinux		+= --defsym 'jiffies=jiffies_64+4'
LDFLAGS			+= -EB
endif

CFLAGS		+= -pipe $(cflags-y)
AFLAGS		+= $(cflags-y)

head-y := arch/sh/kernel/head.o arch/sh/kernel/init_task.o

LIBGCC := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

core-y				+= arch/sh/kernel/ arch/sh/mm/
core-$(CONFIG_SH_FPU_EMU)	+= arch/sh/math-emu/

# Boards
machdir-$(CONFIG_SH_SOLUTION_ENGINE)		:= se/770x
machdir-$(CONFIG_SH_7751_SOLUTION_ENGINE)	:= se/7751
machdir-$(CONFIG_SH_7300_SOLUTION_ENGINE)	:= se/7300
machdir-$(CONFIG_SH_7343_SOLUTION_ENGINE)	:= se/7343
machdir-$(CONFIG_SH_73180_SOLUTION_ENGINE)	:= se/73180
machdir-$(CONFIG_SH_HP6XX)			:= hp6xx
machdir-$(CONFIG_SH_EC3104)			:= ec3104
machdir-$(CONFIG_SH_SATURN)			:= saturn
machdir-$(CONFIG_SH_DREAMCAST)			:= dreamcast
machdir-$(CONFIG_SH_BIGSUR)			:= bigsur
machdir-$(CONFIG_SH_MPC1211)			:= mpc1211
machdir-$(CONFIG_SH_SH03)			:= sh03
machdir-$(CONFIG_SH_SECUREEDGE5410)		:= snapgear
machdir-$(CONFIG_SH_HS7751RVOIP)		:= renesas/hs7751rvoip
machdir-$(CONFIG_SH_RTS7751R2D)			:= renesas/rts7751r2d
machdir-$(CONFIG_SH_7751_SYSTEMH)		:= renesas/systemh
machdir-$(CONFIG_SH_EDOSK7705)			:= renesas/edosk7705
machdir-$(CONFIG_SH_R7780RP)			:= renesas/r7780rp
machdir-$(CONFIG_SH_7710VOIPGW)			:= renesas/sh7710voipgw
machdir-$(CONFIG_SH_SH4202_MICRODEV)		:= superh/microdev
machdir-$(CONFIG_SH_LANDISK)			:= landisk
machdir-$(CONFIG_SH_TITAN)			:= titan
machdir-$(CONFIG_SH_SHMIN)			:= shmin
machdir-$(CONFIG_SH_7206_SOLUTION_ENGINE)	:= se/7206
machdir-$(CONFIG_SH_7619_SOLUTION_ENGINE)	:= se/7619
machdir-$(CONFIG_SH_UNKNOWN)			:= unknown

incdir-y			:= $(notdir $(machdir-y))
incdir-$(CONFIG_SH_HP6XX)			:= hp6xx

ifneq ($(machdir-y),)
core-y				+= arch/sh/boards/$(machdir-y)/
endif

# Companion chips
core-$(CONFIG_HD64461)		+= arch/sh/cchips/hd6446x/hd64461/
core-$(CONFIG_HD64465)		+= arch/sh/cchips/hd6446x/hd64465/
core-$(CONFIG_VOYAGERGX)	+= arch/sh/cchips/voyagergx/

cpuincdir-$(CONFIG_CPU_SH2)	:= cpu-sh2
cpuincdir-$(CONFIG_CPU_SH2A)	:= cpu-sh2a
cpuincdir-$(CONFIG_CPU_SH3)	:= cpu-sh3
cpuincdir-$(CONFIG_CPU_SH4)	:= cpu-sh4

libs-y				:= arch/sh/lib/	$(libs-y) $(LIBGCC)

drivers-y			+= arch/sh/drivers/
drivers-$(CONFIG_OPROFILE)	+= arch/sh/oprofile/

boot := arch/sh/boot

CPPFLAGS_vmlinux.lds := -traditional

incdir-prefix	:= $(srctree)/include/asm-sh/

#	Update machine arch and proc symlinks if something which affects
#	them changed.  We use .arch and .mach to indicate when they were
#	updated last, otherwise make uses the target directory mtime.

include/asm-sh/.cpu: $(wildcard include/config/cpu/*.h) \
		     include/config/auto.conf FORCE
	@echo '  SYMLINK include/asm-sh/cpu -> include/asm-sh/$(cpuincdir-y)'
	$(Q)if [ ! -d include/asm-sh ]; then mkdir -p include/asm-sh; fi
	$(Q)ln -fsn $(incdir-prefix)$(cpuincdir-y) include/asm-sh/cpu
	@touch $@

#	Most boards have their own mach directories.  For the ones that
#	don't, just reference the parent directory so the semantics are
#	kept roughly the same.

include/asm-sh/.mach: $(wildcard include/config/sh/*.h) \
		      include/config/auto.conf FORCE
	@echo -n '  SYMLINK include/asm-sh/mach -> '
	$(Q)if [ ! -d include/asm-sh ]; then mkdir -p include/asm-sh; fi
	$(Q)if [ -d $(incdir-prefix)$(incdir-y) ]; then \
		echo -e 'include/asm-sh/$(incdir-y)'; \
		ln -fsn $(incdir-prefix)$(incdir-y) \
			include/asm-sh/mach; \
	else \
		echo -e 'include/asm-sh'; \
		ln -fsn $(incdir-prefix) include/asm-sh/mach; \
	fi
	@touch $@

archprepare: include/asm-sh/.cpu include/asm-sh/.mach maketools

PHONY += maketools FORCE
maketools:  include/linux/version.h FORCE
	$(Q)$(MAKE) $(build)=arch/sh/tools include/asm-sh/machtypes.h

all: zImage

zImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

compressed: zImage

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

CLEAN_FILES += include/asm-sh/machtypes.h

define archhelp
	@echo '  zImage 	           - Compressed kernel image (arch/sh/boot/zImage)'
endef
