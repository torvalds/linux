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

isa-y			:= $(isa-y)-up

cflags-$(CONFIG_CPU_SH2)		:= $(call cc-option,-m2,)
cflags-$(CONFIG_CPU_SH2A)		+= $(call cc-option,-m2a,) \
					   $(call cc-option,-m2a-nofpu,)
cflags-$(CONFIG_CPU_SH3)		:= $(call cc-option,-m3,)
cflags-$(CONFIG_CPU_SH4)		:= $(call cc-option,-m4,) \
	$(call cc-option,-mno-implicit-fp,-m4-nofpu)
cflags-$(CONFIG_CPU_SH4A)		+= $(call cc-option,-m4a,) \
					   $(call cc-option,-m4a-nofpu,)

cflags-$(CONFIG_CPU_BIG_ENDIAN)		+= -mb
cflags-$(CONFIG_CPU_LITTLE_ENDIAN)	+= -ml

#
# -Wa,-isa= tuning implies -Wa,-dsp for the versions of binutils that
# support it, while -Wa,-dsp by itself limits the range of usable opcodes
# on certain CPU subtypes. Try the ISA variant first, and if that fails,
# fall back on -Wa,-dsp for the old binutils versions. Even without DSP
# opcodes, we always want the best ISA tuning the version of binutils
# will provide.
#
isaflags-y	:= $(call as-option,-Wa$(comma)-isa=$(isa-y),)

isaflags-$(CONFIG_SH_DSP)		:= \
	$(call as-option,-Wa$(comma)-isa=$(isa-y),-Wa$(comma)-dsp)

cflags-y	+= $(isaflags-y) -ffreestanding

cflags-$(CONFIG_MORE_COMPILE_OPTIONS)	+= \
	$(shell echo $(CONFIG_COMPILE_OPTIONS) | sed -e 's/"//g')

OBJCOPYFLAGS	:= -O binary -R .note -R .note.gnu.build-id -R .comment -R .stab -R .stabstr -S

#
# arch/sh/defconfig doesn't reflect any real hardware, and as such should
# never be used by anyone. Use a board-specific defconfig that has a
# reasonable chance of being current instead.
#
KBUILD_DEFCONFIG := r7780rp_defconfig

KBUILD_IMAGE	:= arch/sh/boot/zImage

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

KBUILD_CFLAGS		+= -pipe $(cflags-y)
KBUILD_AFLAGS		+= $(cflags-y)

head-y := arch/sh/kernel/head.o arch/sh/kernel/init_task.o

LIBGCC := $(shell $(CC) $(KBUILD_CFLAGS) -print-libgcc-file-name)

core-y				+= arch/sh/kernel/ arch/sh/mm/
core-$(CONFIG_SH_FPU_EMU)	+= arch/sh/math-emu/

# Boards
machdir-$(CONFIG_SH_SOLUTION_ENGINE)		+= se/770x
machdir-$(CONFIG_SH_7722_SOLUTION_ENGINE)	+= se/7722
machdir-$(CONFIG_SH_7751_SOLUTION_ENGINE)	+= se/7751
machdir-$(CONFIG_SH_7780_SOLUTION_ENGINE)	+= se/7780
machdir-$(CONFIG_SH_7343_SOLUTION_ENGINE)	+= se/7343
machdir-$(CONFIG_SH_HP6XX)			+= hp6xx
machdir-$(CONFIG_SH_DREAMCAST)			+= dreamcast
machdir-$(CONFIG_SH_MPC1211)			+= mpc1211
machdir-$(CONFIG_SH_SH03)			+= sh03
machdir-$(CONFIG_SH_SECUREEDGE5410)		+= snapgear
machdir-$(CONFIG_SH_HS7751RVOIP)		+= renesas/hs7751rvoip
machdir-$(CONFIG_SH_RTS7751R2D)			+= renesas/rts7751r2d
machdir-$(CONFIG_SH_7751_SYSTEMH)		+= renesas/systemh
machdir-$(CONFIG_SH_EDOSK7705)			+= renesas/edosk7705
machdir-$(CONFIG_SH_HIGHLANDER)			+= renesas/r7780rp
machdir-$(CONFIG_SH_7710VOIPGW)			+= renesas/sh7710voipgw
machdir-$(CONFIG_SH_X3PROTO)			+= renesas/x3proto
machdir-$(CONFIG_SH_SH4202_MICRODEV)		+= superh/microdev
machdir-$(CONFIG_SH_LANDISK)			+= landisk
machdir-$(CONFIG_SH_TITAN)			+= titan
machdir-$(CONFIG_SH_SHMIN)			+= shmin
machdir-$(CONFIG_SH_7206_SOLUTION_ENGINE)	+= se/7206
machdir-$(CONFIG_SH_7619_SOLUTION_ENGINE)	+= se/7619
machdir-$(CONFIG_SH_LBOX_RE2)			+= lboxre2
machdir-$(CONFIG_SH_MAGIC_PANEL_R2)		+= magicpanelr2

incdir-y	:= $(notdir $(machdir-y))

ifneq ($(machdir-y),)
core-y	+= $(addprefix arch/sh/boards/, \
	     $(filter-out ., $(patsubst %,%/,$(machdir-y))))
endif

# Companion chips
core-$(CONFIG_HD6446X_SERIES)	+= arch/sh/cchips/hd6446x/
core-$(CONFIG_MFD_SM501)	+= arch/sh/cchips/voyagergx/

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
#
#	When multiple boards are compiled in at the same time, preference
#	for the mach link is given to whichever has a directory for its
#	headers. However, this is only a workaround until platforms that
#	can live in the same kernel image back away from relying on the
#	mach link.

include/asm-sh/.mach: $(wildcard include/config/sh/*.h) \
		      include/config/auto.conf FORCE
	$(Q)if [ ! -d include/asm-sh ]; then mkdir -p include/asm-sh; fi
	$(Q)rm -f include/asm-sh/mach
	$(Q)for i in $(incdir-y); do \
	if [ -d $(incdir-prefix)$$i ]; then \
		echo -n '  SYMLINK include/asm-sh/mach -> '; \
		echo -e "include/asm-sh/$$i"; \
		ln -fsn $(incdir-prefix)$$i \
			include/asm-sh/mach; \
	else \
		if [ ! -d include/asm-sh/mach ]; then \
			echo -n '  SYMLINK include/asm-sh/mach -> '; \
			echo -e 'include/asm-sh'; \
			ln -fsn $(incdir-prefix) include/asm-sh/mach; \
		fi; \
	fi; \
	done
	@touch $@

archprepare: include/asm-sh/.cpu include/asm-sh/.mach maketools

PHONY += maketools FORCE
maketools:  include/linux/version.h FORCE
	$(Q)$(MAKE) $(build)=arch/sh/tools include/asm-sh/machtypes.h

all: zImage

zImage uImage uImage.srec vmlinux.srec: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

compressed: zImage

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

CLEAN_FILES += include/asm-sh/machtypes.h \
	       include/asm-sh/cpu include/asm-sh/.cpu \
	       include/asm-sh/mach include/asm-sh/.mach

define archhelp
	@echo '* zImage 	           - Compressed kernel image'
	@echo '  vmlinux.srec	           - Create an ELF S-record'
	@echo '  uImage  	           - Create a bootable image for U-Boot'
	@echo '  uImage.srec  	           - Create an S-record for U-Boot'
endef
