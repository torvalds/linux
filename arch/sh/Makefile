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

OBJCOPYFLAGS	:= -O binary -R .note -R .note.gnu.build-id -R .comment \
		   -R .stab -R .stabstr -S

# Give the various platforms the opportunity to set default image types
defaultimage-$(CONFIG_SUPERH32)			:= zImage
defaultimage-$(CONFIG_SH_SH7785LCR)		:= uImage
defaultimage-$(CONFIG_SH_RSK)			:= uImage
defaultimage-$(CONFIG_SH_URQUELL)		:= uImage
defaultimage-$(CONFIG_SH_MIGOR)			:= uImage
defaultimage-$(CONFIG_SH_AP325RXA)		:= uImage
defaultimage-$(CONFIG_SH_7724_SOLUTION_ENGINE)	:= uImage
defaultimage-$(CONFIG_SH_7206_SOLUTION_ENGINE)	:= vmlinux
defaultimage-$(CONFIG_SH_7619_SOLUTION_ENGINE)	:= vmlinux

# Set some sensible Kbuild defaults
KBUILD_IMAGE		:= $(defaultimage-y)

#
# Choosing incompatible machines durings configuration will result in
# error messages during linking.
#
ifdef CONFIG_SUPERH32
UTS_MACHINE		:= sh
BITS			:= 32
LDFLAGS_vmlinux		+= -e _stext
KBUILD_DEFCONFIG	:= shx3_defconfig
else
UTS_MACHINE		:= sh64
BITS			:= 64
LDFLAGS_vmlinux		+= --defsym phys_stext=_stext-$(CONFIG_PAGE_OFFSET) \
			   --defsym phys_stext_shmedia=phys_stext+1 \
			   -e phys_stext_shmedia
KBUILD_DEFCONFIG	:= cayman_defconfig
endif

ifneq ($(SUBARCH),$(ARCH))
  ifeq ($(CROSS_COMPILE),)
    CROSS_COMPILE := $(call cc-cross-prefix, $(UTS_MACHINE)-linux-  $(UTS_MACHINE)-linux-gnu-  $(UTS_MACHINE)-unknown-linux-gnu-)
  endif
endif

ifdef CONFIG_CPU_LITTLE_ENDIAN
ld-bfd			:= elf32-$(UTS_MACHINE)-linux
LDFLAGS_vmlinux		+= --defsym 'jiffies=jiffies_64' --oformat $(ld-bfd)
LDFLAGS			+= -EL
else
ld-bfd			:= elf32-$(UTS_MACHINE)big-linux
LDFLAGS_vmlinux		+= --defsym 'jiffies=jiffies_64+4' --oformat $(ld-bfd)
LDFLAGS			+= -EB
endif

export ld-bfd BITS

head-y	:= arch/sh/kernel/init_task.o arch/sh/kernel/head_$(BITS).o

core-y				+= arch/sh/kernel/ arch/sh/mm/ arch/sh/boards/
core-$(CONFIG_SH_FPU_EMU)	+= arch/sh/math-emu/

# Mach groups
machdir-$(CONFIG_SOLUTION_ENGINE)		+= mach-se
machdir-$(CONFIG_SH_HP6XX)			+= mach-hp6xx
machdir-$(CONFIG_SH_DREAMCAST)			+= mach-dreamcast
machdir-$(CONFIG_SH_SH03)			+= mach-sh03
machdir-$(CONFIG_SH_RTS7751R2D)			+= mach-r2d
machdir-$(CONFIG_SH_HIGHLANDER)			+= mach-highlander
machdir-$(CONFIG_SH_MIGOR)			+= mach-migor
machdir-$(CONFIG_SH_AP325RXA)			+= mach-ap325rxa
machdir-$(CONFIG_SH_KFR2R09)			+= mach-kfr2r09
machdir-$(CONFIG_SH_ECOVEC)			+= mach-ecovec24
machdir-$(CONFIG_SH_SDK7780)			+= mach-sdk7780
machdir-$(CONFIG_SH_SDK7786)			+= mach-sdk7786
machdir-$(CONFIG_SH_X3PROTO)			+= mach-x3proto
machdir-$(CONFIG_SH_SH7763RDP)			+= mach-sh7763rdp
machdir-$(CONFIG_SH_SH4202_MICRODEV)		+= mach-microdev
machdir-$(CONFIG_SH_LANDISK)			+= mach-landisk
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

ifeq ($(CONFIG_MCOUNT),y)
  KBUILD_CFLAGS += -pg
endif

ifeq ($(CONFIG_DWARF_UNWINDER),y)
  KBUILD_CFLAGS += -fasynchronous-unwind-tables
endif

libs-$(CONFIG_SUPERH32)		:= arch/sh/lib/	$(libs-y)
libs-$(CONFIG_SUPERH64)		:= arch/sh/lib64/ $(libs-y)

BOOT_TARGETS = uImage uImage.bz2 uImage.gz uImage.lzma uImage.lzo \
	       uImage.srec uImage.bin zImage vmlinux.bin vmlinux.srec \
	       romImage
PHONY += $(BOOT_TARGETS)

all: $(KBUILD_IMAGE)

$(BOOT_TARGETS): vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

compressed: zImage

archprepare:
	$(Q)$(MAKE) $(build)=arch/sh/tools include/generated/machtypes.h

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)
	$(Q)$(MAKE) $(clean)=arch/sh/kernel/vsyscall

define archhelp
	@echo '  zImage 	           - Compressed kernel image'
	@echo '  romImage	           - Compressed ROM image, if supported'
	@echo '  vmlinux.srec	           - Create an ELF S-record'
	@echo '  vmlinux.bin	           - Create an uncompressed binary image'
	@echo '* uImage  	           - Alias to bootable U-Boot image'
	@echo '  uImage.srec	           - Create an S-record for U-Boot'
	@echo '  uImage.bin	           - Kernel-only image for U-Boot (bin)'
	@echo '* uImage.gz	           - Kernel-only image for U-Boot (gzip)'
	@echo '  uImage.bz2	           - Kernel-only image for U-Boot (bzip2)'
	@echo '  uImage.lzma	           - Kernel-only image for U-Boot (lzma)'
	@echo '  uImage.lzo	           - Kernel-only image for U-Boot (lzo)'
endef
