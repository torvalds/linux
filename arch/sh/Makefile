#
# arch/sh/Makefile
#
# Copyright (C) 1999  Kaz Kojima
# Copyright (C) 2002 - 2008  Paul Mundt
# Copyright (C) 2002  M. R. Brown
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
isa-y					:= any
isa-$(CONFIG_SH_DSP)			:= sh
isa-$(CONFIG_CPU_SH2)			:= sh2
isa-$(CONFIG_CPU_SH2A)			:= sh2a
isa-$(CONFIG_CPU_SH3)			:= sh3
isa-$(CONFIG_CPU_SH4)			:= sh4
isa-$(CONFIG_CPU_SH4A)			:= sh4a
isa-$(CONFIG_CPU_SH4AL_DSP)		:= sh4al
isa-$(CONFIG_CPU_SH5)			:= shmedia

ifeq ($(CONFIG_SUPERH32),y)
isa-$(CONFIG_SH_DSP)			:= $(isa-y)-dsp
isa-y					:= $(isa-y)-up
endif

cflags-$(CONFIG_CPU_SH2)		:= $(call cc-option,-m2,)
cflags-$(CONFIG_CPU_SH2A)		+= $(call cc-option,-m2a,) \
					   $(call cc-option,-m2a-nofpu,)
cflags-$(CONFIG_CPU_SH3)		:= $(call cc-option,-m3,)
cflags-$(CONFIG_CPU_SH4)		:= $(call cc-option,-m4,) \
	$(call cc-option,-mno-implicit-fp,-m4-nofpu)
cflags-$(CONFIG_CPU_SH4A)		+= $(call cc-option,-m4a,) \
					   $(call cc-option,-m4a-nofpu,)
cflags-$(CONFIG_CPU_SH4AL_DSP)		+= $(call cc-option,-m4al,)
cflags-$(CONFIG_CPU_SH5)		:= $(call cc-option,-m5-32media-nofpu,)

ifeq ($(cflags-y),)
#
# In the case where we are stuck with a compiler that has been uselessly
# restricted to a particular ISA, a favourite default of newer GCCs when
# extensive multilib targets are not provided, ensure we get the best fit
# regarding FP generation. This is intentionally stupid (albeit many
# orders of magnitude less than GCC's default behaviour), as anything
# with a large number of multilib targets better have been built
# correctly for the target in mind.
#
cflags-y	+= $(shell $(CC) $(KBUILD_CFLAGS) -print-multi-lib | \
		     grep nofpu | sed q | sed -e 's/^/-/;s/;.*$$//')
# At this point, anything goes.
isaflags-y	:= $(call as-option,-Wa$(comma)-isa=any,)
else
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
endif

cflags-$(CONFIG_CPU_BIG_ENDIAN)		+= -mb
cflags-$(CONFIG_CPU_LITTLE_ENDIAN)	+= -ml

cflags-y	+= $(call cc-option,-mno-fdpic)
cflags-y	+= $(isaflags-y) -ffreestanding

cflags-$(CONFIG_MORE_COMPILE_OPTIONS)	+= \
	$(shell echo $(CONFIG_COMPILE_OPTIONS) | sed -e 's/"//g')

OBJCOPYFLAGS	:= -O binary -R .note -R .note.gnu.build-id -R .comment \
		   -R .stab -R .stabstr -S

# Give the various platforms the opportunity to set default image types
defaultimage-$(CONFIG_SUPERH32)			:= zImage
defaultimage-$(CONFIG_SH_SH7785LCR)		:= uImage
defaultimage-$(CONFIG_SH_RSK)			:= uImage
defaultimage-$(CONFIG_SH_URQUELL)		:= uImage
defaultimage-$(CONFIG_SH_7206_SOLUTION_ENGINE)	:= vmlinux
defaultimage-$(CONFIG_SH_7619_SOLUTION_ENGINE)	:= vmlinux

# Set some sensible Kbuild defaults
KBUILD_DEFCONFIG	:= shx3_defconfig
KBUILD_IMAGE		:= $(defaultimage-y)

#
# Choosing incompatible machines durings configuration will result in
# error messages during linking.
#
ifdef CONFIG_SUPERH32
UTS_MACHINE	:= sh
LDFLAGS_vmlinux	+= -e _stext
else
UTS_MACHINE	:= sh64
LDFLAGS_vmlinux	+= --defsym phys_stext=_stext-$(CONFIG_PAGE_OFFSET) \
		   --defsym phys_stext_shmedia=phys_stext+1 \
		   -e phys_stext_shmedia
endif

ifdef CONFIG_CPU_LITTLE_ENDIAN
LDFLAGS_vmlinux		+= --defsym 'jiffies=jiffies_64'
LDFLAGS			+= -EL
else
LDFLAGS_vmlinux		+= --defsym 'jiffies=jiffies_64+4'
LDFLAGS			+= -EB
endif

head-y			:= arch/sh/kernel/init_task.o
head-$(CONFIG_SUPERH32)	+= arch/sh/kernel/head_32.o
head-$(CONFIG_SUPERH64)	+= arch/sh/kernel/head_64.o

core-y				+= arch/sh/kernel/ arch/sh/mm/ arch/sh/boards/
core-$(CONFIG_SH_FPU_EMU)	+= arch/sh/math-emu/

# Mach groups
machdir-$(CONFIG_SOLUTION_ENGINE)		+= mach-se
machdir-$(CONFIG_SH_HP6XX)			+= mach-hp6xx
machdir-$(CONFIG_SH_DREAMCAST)			+= mach-dreamcast
machdir-$(CONFIG_SH_SH03)			+= mach-sh03
machdir-$(CONFIG_SH_SECUREEDGE5410)		+= mach-snapgear
machdir-$(CONFIG_SH_RTS7751R2D)			+= mach-r2d
machdir-$(CONFIG_SH_7751_SYSTEMH)		+= mach-systemh
machdir-$(CONFIG_SH_EDOSK7705)			+= mach-edosk7705
machdir-$(CONFIG_SH_HIGHLANDER)			+= mach-highlander
machdir-$(CONFIG_SH_MIGOR)			+= mach-migor
machdir-$(CONFIG_SH_SDK7780)			+= mach-sdk7780
machdir-$(CONFIG_SH_X3PROTO)			+= mach-x3proto
machdir-$(CONFIG_SH_SH7763RDP)			+= mach-sh7763rdp
machdir-$(CONFIG_SH_SH4202_MICRODEV)		+= mach-microdev
machdir-$(CONFIG_SH_LANDISK)			+= mach-landisk
machdir-$(CONFIG_SH_TITAN)			+= mach-titan
machdir-$(CONFIG_SH_LBOX_RE2)			+= mach-lboxre2
machdir-$(CONFIG_SH_CAYMAN)			+= mach-cayman
machdir-$(CONFIG_SH_RSK)			+= mach-rsk

ifneq ($(machdir-y),)
core-y	+= $(addprefix arch/sh/boards/, \
	     $(filter-out ., $(patsubst %,%/,$(machdir-y))))
endif

# Common machine type headers. Not part of the arch/sh/boards/ hierarchy.
machdir-y	+= mach-common

# Companion chips
core-$(CONFIG_HD6446X_SERIES)	+= arch/sh/cchips/hd6446x/

#
# CPU header paths
#
# These are ordered by optimization level. A CPU family that is a subset
# of another (ie, SH-2A / SH-2), is picked up first, with increasing
# levels of genericness if nothing more suitable is situated in the
# hierarchy.
#
# As an example, in order of preference, SH-2A > SH-2 > common definitions.
#
cpuincdir-$(CONFIG_CPU_SH2A)	+= cpu-sh2a
cpuincdir-$(CONFIG_CPU_SH2)	+= cpu-sh2
cpuincdir-$(CONFIG_CPU_SH3)	+= cpu-sh3
cpuincdir-$(CONFIG_CPU_SH4)	+= cpu-sh4
cpuincdir-$(CONFIG_CPU_SH5)	+= cpu-sh5
cpuincdir-y			+= cpu-common	# Must be last

drivers-y			+= arch/sh/drivers/
drivers-$(CONFIG_OPROFILE)	+= arch/sh/oprofile/

boot := arch/sh/boot

cflags-y	+= $(foreach d, $(cpuincdir-y), -Iarch/sh/include/$(d)) \
		   $(foreach d, $(machdir-y), -Iarch/sh/include/$(d))

KBUILD_CFLAGS		+= -pipe $(cflags-y)
KBUILD_CPPFLAGS		+= $(cflags-y)
KBUILD_AFLAGS		+= $(cflags-y)

libs-$(CONFIG_SUPERH32)		:= arch/sh/lib/	$(libs-y)
libs-$(CONFIG_SUPERH64)		:= arch/sh/lib64/ $(libs-y)

PHONY += maketools FORCE

maketools:  include/linux/version.h FORCE
	$(Q)$(MAKE) $(build)=arch/sh/tools include/asm-sh/machtypes.h

all: $(KBUILD_IMAGE)

zImage uImage uImage.srec vmlinux.srec: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

compressed: zImage

archprepare: maketools arch/sh/lib64/syscalltab.h

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

define archhelp
	@echo '* zImage 	           - Compressed kernel image'
	@echo '  vmlinux.srec	           - Create an ELF S-record'
	@echo '  uImage  	           - Create a bootable image for U-Boot'
	@echo '  uImage.srec  	           - Create an S-record for U-Boot'
endef

define filechk_gen-syscalltab
       (set -e; \
	echo "/*"; \
	echo " * DO NOT MODIFY."; \
	echo " *"; \
	echo " * This file was generated by arch/sh/Makefile"; \
	echo " * Any changes will be reverted at build time."; \
	echo " */"; \
	echo ""; \
	echo "#ifndef __SYSCALLTAB_H"; \
	echo "#define __SYSCALLTAB_H"; \
	echo ""; \
	echo "#include <linux/kernel.h>"; \
	echo ""; \
	echo "struct syscall_info {"; \
	echo "	const char *name;"; \
	echo "} syscall_info_table[] = {"; \
	sed -e '/^.*\.long /!d;s//	{ "/;s/\(\([^/]*\)\/\)\{1\}.*/\2/; \
		s/[ \t]*$$//g;s/$$/" },/;s/\("\)sys_/\1/g'; \
	echo "};"; \
	echo ""; \
	echo "#define NUM_SYSCALL_INFO_ENTRIES ARRAY_SIZE(syscall_info_table)";\
	echo ""; \
	echo "#endif /* __SYSCALLTAB_H */" )
endef

arch/sh/lib64/syscalltab.h: arch/sh/kernel/syscalls_64.S
	$(call filechk,gen-syscalltab)

CLEAN_FILES += arch/sh/lib64/syscalltab.h \
	       include/asm-sh/machtypes.h
