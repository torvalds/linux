#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994, 95, 96, 2003 by Ralf Baechle
# DECStation modifications by Paul M. Antoine, 1996
# Copyright (C) 2002, 2003, 2004  Maciej W. Rozycki
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" cleaning up for this architecture.
#

archscripts: scripts_basic
	$(Q)$(MAKE) $(build)=arch/mips/boot/tools relocs

KBUILD_DEFCONFIG := ip22_defconfig

#
# Select the object file format to substitute into the linker script.
#
ifdef CONFIG_CPU_LITTLE_ENDIAN
32bit-tool-archpref	= mipsel
64bit-tool-archpref	= mips64el
32bit-bfd		= elf32-tradlittlemips
64bit-bfd		= elf64-tradlittlemips
32bit-emul		= elf32ltsmip
64bit-emul		= elf64ltsmip
else
32bit-tool-archpref	= mips
64bit-tool-archpref	= mips64
32bit-bfd		= elf32-tradbigmips
64bit-bfd		= elf64-tradbigmips
32bit-emul		= elf32btsmip
64bit-emul		= elf64btsmip
endif

ifdef CONFIG_32BIT
tool-archpref		= $(32bit-tool-archpref)
UTS_MACHINE		:= mips
endif
ifdef CONFIG_64BIT
tool-archpref		= $(64bit-tool-archpref)
UTS_MACHINE		:= mips64
endif

ifneq ($(SUBARCH),$(ARCH))
  ifeq ($(CROSS_COMPILE),)
    CROSS_COMPILE := $(call cc-cross-prefix, $(tool-archpref)-linux-  $(tool-archpref)-linux-gnu-  $(tool-archpref)-unknown-linux-gnu-)
  endif
endif

ifdef CONFIG_FUNCTION_GRAPH_TRACER
  ifndef KBUILD_MCOUNT_RA_ADDRESS
    ifeq ($(call cc-option-yn,-mmcount-ra-address), y)
      cflags-y += -mmcount-ra-address -DKBUILD_MCOUNT_RA_ADDRESS
    endif
  endif
endif
cflags-y += $(call cc-option, -mno-check-zero-division)

ifdef CONFIG_32BIT
ld-emul			= $(32bit-emul)
vmlinux-32		= vmlinux
vmlinux-64		= vmlinux.64

cflags-y		+= -mabi=32
endif

ifdef CONFIG_64BIT
ld-emul			= $(64bit-emul)
vmlinux-32		= vmlinux.32
vmlinux-64		= vmlinux

cflags-y		+= -mabi=64
endif

all-$(CONFIG_BOOT_ELF32)	:= $(vmlinux-32)
all-$(CONFIG_BOOT_ELF64)	:= $(vmlinux-64)
all-$(CONFIG_SYS_SUPPORTS_ZBOOT)+= vmlinuz

#
# GCC uses -G 0 -mabicalls -fpic as default.  We don't want PIC in the kernel
# code since it only slows down the whole thing.  At some point we might make
# use of global pointer optimizations but their use of $28 conflicts with
# the current pointer optimization.
#
# The DECStation requires an ECOFF kernel for remote booting, other MIPS
# machines may also.  Since BFD is incredibly buggy with respect to
# crossformat linking we rely on the elf2ecoff tool for format conversion.
#
cflags-y			+= -G 0 -mno-abicalls -fno-pic -pipe
cflags-y			+= -msoft-float
LDFLAGS_vmlinux			+= -G 0 -static -n -nostdlib
KBUILD_AFLAGS_MODULE		+= -mlong-calls
KBUILD_CFLAGS_MODULE		+= -mlong-calls

ifeq ($(CONFIG_RELOCATABLE),y)
LDFLAGS_vmlinux			+= --emit-relocs
endif

#
# pass -msoft-float to GAS if it supports it.  However on newer binutils
# (specifically newer than 2.24.51.20140728) we then also need to explicitly
# set ".set hardfloat" in all files which manipulate floating point registers.
#
ifneq ($(call as-option,-Wa$(comma)-msoft-float,),)
	cflags-y		+= -DGAS_HAS_SET_HARDFLOAT -Wa,-msoft-float
endif

cflags-y += -ffreestanding

#
# We explicitly add the endianness specifier if needed, this allows
# to compile kernels with a toolchain for the other endianness. We
# carefully avoid to add it redundantly because gcc 3.3/3.4 complains
# when fed the toolchain default!
#
# Certain gcc versions up to gcc 4.1.1 (probably 4.2-subversion as of
# 2006-10-10 don't properly change the predefined symbols if -EB / -EL
# are used, so we kludge that here.  A bug has been filed at
# http://gcc.gnu.org/bugzilla/show_bug.cgi?id=29413.
#
undef-all += -UMIPSEB -U_MIPSEB -U__MIPSEB -U__MIPSEB__
undef-all += -UMIPSEL -U_MIPSEL -U__MIPSEL -U__MIPSEL__
predef-be += -DMIPSEB -D_MIPSEB -D__MIPSEB -D__MIPSEB__
predef-le += -DMIPSEL -D_MIPSEL -D__MIPSEL -D__MIPSEL__
cflags-$(CONFIG_CPU_BIG_ENDIAN)		+= $(shell $(CC) -dumpmachine |grep -q 'mips.*el-.*' && echo -EB $(undef-all) $(predef-be))
cflags-$(CONFIG_CPU_LITTLE_ENDIAN)	+= $(shell $(CC) -dumpmachine |grep -q 'mips.*el-.*' || echo -EL $(undef-all) $(predef-le))

cflags-$(CONFIG_SB1XXX_CORELIS)	+= $(call cc-option,-mno-sched-prolog) \
				   -fno-omit-frame-pointer
#
# CPU-dependent compiler/assembler options for optimization.
#
cflags-$(CONFIG_CPU_R3000)	+= -march=r3000
cflags-$(CONFIG_CPU_TX39XX)	+= -march=r3900
cflags-$(CONFIG_CPU_R6000)	+= -march=r6000 -Wa,--trap
cflags-$(CONFIG_CPU_R4300)	+= -march=r4300 -Wa,--trap
cflags-$(CONFIG_CPU_VR41XX)	+= -march=r4100 -Wa,--trap
cflags-$(CONFIG_CPU_R4X00)	+= -march=r4600 -Wa,--trap
cflags-$(CONFIG_CPU_TX49XX)	+= -march=r4600 -Wa,--trap
cflags-$(CONFIG_CPU_MIPS32_R1)	+= $(call cc-option,-march=mips32,-mips32 -U_MIPS_ISA -D_MIPS_ISA=_MIPS_ISA_MIPS32) \
			-Wa,-mips32 -Wa,--trap
cflags-$(CONFIG_CPU_MIPS32_R2)	+= $(call cc-option,-march=mips32r2,-mips32r2 -U_MIPS_ISA -D_MIPS_ISA=_MIPS_ISA_MIPS32) \
			-Wa,-mips32r2 -Wa,--trap
cflags-$(CONFIG_CPU_MIPS32_R6)	+= -march=mips32r6 -Wa,--trap
cflags-$(CONFIG_CPU_MIPS64_R1)	+= $(call cc-option,-march=mips64,-mips64 -U_MIPS_ISA -D_MIPS_ISA=_MIPS_ISA_MIPS64) \
			-Wa,-mips64 -Wa,--trap
cflags-$(CONFIG_CPU_MIPS64_R2)	+= $(call cc-option,-march=mips64r2,-mips64r2 -U_MIPS_ISA -D_MIPS_ISA=_MIPS_ISA_MIPS64) \
			-Wa,-mips64r2 -Wa,--trap
cflags-$(CONFIG_CPU_MIPS64_R6)	+= -march=mips64r6 -Wa,--trap
cflags-$(CONFIG_CPU_R5000)	+= -march=r5000 -Wa,--trap
cflags-$(CONFIG_CPU_R5432)	+= $(call cc-option,-march=r5400,-march=r5000) \
			-Wa,--trap
cflags-$(CONFIG_CPU_R5500)	+= $(call cc-option,-march=r5500,-march=r5000) \
			-Wa,--trap
cflags-$(CONFIG_CPU_NEVADA)	+= $(call cc-option,-march=rm5200,-march=r5000) \
			-Wa,--trap
cflags-$(CONFIG_CPU_RM7000)	+= $(call cc-option,-march=rm7000,-march=r5000) \
			-Wa,--trap
cflags-$(CONFIG_CPU_SB1)	+= $(call cc-option,-march=sb1,-march=r5000) \
			-Wa,--trap
cflags-$(CONFIG_CPU_SB1)	+= $(call cc-option,-mno-mdmx)
cflags-$(CONFIG_CPU_SB1)	+= $(call cc-option,-mno-mips3d)
cflags-$(CONFIG_CPU_R8000)	+= -march=r8000 -Wa,--trap
cflags-$(CONFIG_CPU_R10000)	+= $(call cc-option,-march=r10000,-march=r8000) \
			-Wa,--trap
cflags-$(CONFIG_CPU_CAVIUM_OCTEON) += $(call cc-option,-march=octeon) -Wa,--trap
ifeq (,$(findstring march=octeon, $(cflags-$(CONFIG_CPU_CAVIUM_OCTEON))))
cflags-$(CONFIG_CPU_CAVIUM_OCTEON) += -Wa,-march=octeon
endif
cflags-$(CONFIG_CAVIUM_CN63XXP1) += -Wa,-mfix-cn63xxp1
cflags-$(CONFIG_CPU_BMIPS)	+= -march=mips32 -Wa,-mips32 -Wa,--trap

cflags-$(CONFIG_CPU_R4000_WORKAROUNDS)	+= $(call cc-option,-mfix-r4000,)
cflags-$(CONFIG_CPU_R4400_WORKAROUNDS)	+= $(call cc-option,-mfix-r4400,)
cflags-$(CONFIG_CPU_DADDI_WORKAROUNDS)	+= $(call cc-option,-mno-daddi,)

# For smartmips configurations, there are hundreds of warnings due to ISA overrides
# in assembly and header files. smartmips is only supported for MIPS32r1 onwards
# and there is no support for 64-bit. Various '.set mips2' or '.set mips3' or
# similar directives in the kernel will spam the build logs with the following warnings:
# Warning: the `smartmips' extension requires MIPS32 revision 1 or greater
# or
# Warning: the 64-bit MIPS architecture does not support the `smartmips' extension
# Pass -Wa,--no-warn to disable all assembler warnings until the kernel code has
# been fixed properly.
mips-cflags				:= $(cflags-y)
ifeq ($(CONFIG_CPU_HAS_SMARTMIPS),y)
smartmips-ase				:= $(call cc-option-yn,$(mips-cflags) -msmartmips)
cflags-$(smartmips-ase)			+= -msmartmips -Wa,--no-warn
endif
ifeq ($(CONFIG_CPU_MICROMIPS),y)
micromips-ase				:= $(call cc-option-yn,$(mips-cflags) -mmicromips)
cflags-$(micromips-ase)			+= -mmicromips
endif
ifeq ($(CONFIG_CPU_HAS_MSA),y)
toolchain-msa				:= $(call cc-option-yn,$(mips-cflags) -mhard-float -mfp64 -Wa$(comma)-mmsa)
cflags-$(toolchain-msa)			+= -DTOOLCHAIN_SUPPORTS_MSA
endif
toolchain-virt				:= $(call cc-option-yn,$(mips-cflags) -mvirt)
cflags-$(toolchain-virt)		+= -DTOOLCHAIN_SUPPORTS_VIRT

#
# Firmware support
#
libs-$(CONFIG_FW_ARC)		+= arch/mips/fw/arc/
libs-$(CONFIG_FW_CFE)		+= arch/mips/fw/cfe/
libs-$(CONFIG_FW_SNIPROM)	+= arch/mips/fw/sni/
libs-y				+= arch/mips/fw/lib/

#
# Kernel compression
#
ifdef SYS_SUPPORTS_ZBOOT
COMPRESSION_FNAME		= vmlinuz
else
COMPRESSION_FNAME		= vmlinux
endif

#
# Board-dependent options and extra files
#
include arch/mips/Kbuild.platforms

ifdef CONFIG_PHYSICAL_START
load-y					= $(CONFIG_PHYSICAL_START)
endif
entry-y				= 0x$(shell $(NM) vmlinux 2>/dev/null \
					| grep "\bkernel_entry\b" | cut -f1 -d \ )

cflags-y			+= -I$(srctree)/arch/mips/include/asm/mach-generic
drivers-$(CONFIG_PCI)		+= arch/mips/pci/

#
# Automatically detect the build format. By default we choose
# the elf format according to the load address.
# We can always force a build with a 64-bits symbol format by
# passing 'KBUILD_SYM32=no' option to the make's command line.
#
ifdef CONFIG_64BIT
  ifndef KBUILD_SYM32
    ifeq ($(shell expr $(load-y) \< 0xffffffff80000000), 0)
      KBUILD_SYM32 = y
    endif
  endif

  ifeq ($(KBUILD_SYM32)$(call cc-option-yn,-msym32), yy)
    cflags-y += -msym32 -DKBUILD_64BIT_SYM32
  else
    ifeq ($(CONFIG_CPU_DADDI_WORKAROUNDS), y)
      $(error CONFIG_CPU_DADDI_WORKAROUNDS unsupported without -msym32)
    endif
  endif
endif

KBUILD_AFLAGS	+= $(cflags-y)
KBUILD_CFLAGS	+= $(cflags-y)
KBUILD_CPPFLAGS += -DVMLINUX_LOAD_ADDRESS=$(load-y)
KBUILD_CPPFLAGS += -DDATAOFFSET=$(if $(dataoffset-y),$(dataoffset-y),0)

bootvars-y	= VMLINUX_LOAD_ADDRESS=$(load-y) \
		  VMLINUX_ENTRY_ADDRESS=$(entry-y) \
		  PLATFORM=$(platform-y)
ifdef CONFIG_32BIT
bootvars-y	+= ADDR_BITS=32
endif
ifdef CONFIG_64BIT
bootvars-y	+= ADDR_BITS=64
endif

LDFLAGS			+= -m $(ld-emul)

ifdef CONFIG_MIPS
CHECKFLAGS += $(shell $(CC) $(KBUILD_CFLAGS) -dM -E -x c /dev/null | \
	egrep -vw '__GNUC_(|MINOR_|PATCHLEVEL_)_' | \
	sed -e "s/^\#define /-D'/" -e "s/ /'='/" -e "s/$$/'/" -e 's/\$$/&&/g')
ifdef CONFIG_64BIT
CHECKFLAGS		+= -m64
endif
endif

OBJCOPYFLAGS		+= --remove-section=.reginfo

head-y := arch/mips/kernel/head.o

libs-y			+= arch/mips/lib/
libs-y			+= arch/mips/math-emu/

# See arch/mips/Kbuild for content of core part of the kernel
core-y += arch/mips/

drivers-$(CONFIG_OPROFILE)	+= arch/mips/oprofile/

# suspend and hibernation support
drivers-$(CONFIG_PM)	+= arch/mips/power/

# boot image targets (arch/mips/boot/)
boot-y			:= vmlinux.bin
boot-y			+= vmlinux.ecoff
boot-y			+= vmlinux.srec
ifeq ($(shell expr $(load-y) \< 0xffffffff80000000 2> /dev/null), 0)
boot-y			+= uImage
boot-y			+= uImage.bin
boot-y			+= uImage.bz2
boot-y			+= uImage.gz
boot-y			+= uImage.lzma
boot-y			+= uImage.lzo
endif
boot-y			+= vmlinux.itb
boot-y			+= vmlinux.gz.itb
boot-y			+= vmlinux.bz2.itb
boot-y			+= vmlinux.lzma.itb
boot-y			+= vmlinux.lzo.itb

# compressed boot image targets (arch/mips/boot/compressed/)
bootz-y			:= vmlinuz
bootz-y			+= vmlinuz.bin
bootz-y			+= vmlinuz.ecoff
bootz-y			+= vmlinuz.srec

ifdef CONFIG_LASAT
rom.bin rom.sw: vmlinux
	$(Q)$(MAKE) $(build)=arch/mips/lasat/image \
		$(bootvars-y) $@
endif

CMD_RELOCS = arch/mips/boot/tools/relocs
quiet_cmd_relocs = RELOCS  $<
      cmd_relocs = $(CMD_RELOCS) $<

#
# Some machines like the Indy need 32-bit ELF binaries for booting purposes.
# Other need ECOFF, so we build a 32-bit ELF binary for them which we then
# convert to ECOFF using elf2ecoff.
#
quiet_cmd_32 = OBJCOPY $@
	cmd_32 = $(OBJCOPY) -O $(32bit-bfd) $(OBJCOPYFLAGS) $< $@
vmlinux.32: vmlinux
ifeq ($(CONFIG_RELOCATABLE)$(CONFIG_64BIT),yy)
# Currently, objcopy fails to handle the relocations in the elf64
# So the relocs tool must be run here to remove them first
	$(call cmd,relocs)
endif
	$(call cmd,32)

#
# The 64-bit ELF tools are pretty broken so at this time we generate 64-bit
# ELF files from 32-bit files by conversion.
#
quiet_cmd_64 = OBJCOPY $@
	cmd_64 = $(OBJCOPY) -O $(64bit-bfd) $(OBJCOPYFLAGS) $< $@
vmlinux.64: vmlinux
	$(call cmd,64)

all:	$(all-y)

# boot
$(boot-y): $(vmlinux-32) FORCE
ifeq ($(CONFIG_RELOCATABLE)$(CONFIG_32BIT),yy)
	$(call cmd,relocs)
endif
	$(Q)$(MAKE) $(build)=arch/mips/boot VMLINUX=$(vmlinux-32) \
		$(bootvars-y) arch/mips/boot/$@

ifdef CONFIG_SYS_SUPPORTS_ZBOOT
# boot/compressed
$(bootz-y): $(vmlinux-32) FORCE
	$(Q)$(MAKE) $(build)=arch/mips/boot/compressed \
		$(bootvars-y) 32bit-bfd=$(32bit-bfd) $@
else
vmlinuz: FORCE
	@echo '   CONFIG_SYS_SUPPORTS_ZBOOT is not enabled'
	/bin/false
endif


CLEAN_FILES += vmlinux.32 vmlinux.64

# device-trees
core-$(CONFIG_BUILTIN_DTB) += arch/mips/boot/dts/

%.dtb %.dtb.S %.dtb.o: | scripts
	$(Q)$(MAKE) $(build)=arch/mips/boot/dts arch/mips/boot/dts/$@

PHONY += dtbs
dtbs: scripts
	$(Q)$(MAKE) $(build)=arch/mips/boot/dts

PHONY += dtbs_install
dtbs_install:
	$(Q)$(MAKE) $(dtbinst)=arch/mips/boot/dts

archprepare:
ifdef CONFIG_MIPS32_N32
	@echo '  Checking missing-syscalls for N32'
	$(Q)$(MAKE) $(build)=. missing-syscalls missing_syscalls_flags="-mabi=n32"
endif
ifdef CONFIG_MIPS32_O32
	@echo '  Checking missing-syscalls for O32'
	$(Q)$(MAKE) $(build)=. missing-syscalls missing_syscalls_flags="-mabi=32"
endif

install:
	$(Q)install -D -m 755 vmlinux $(INSTALL_PATH)/vmlinux-$(KERNELRELEASE)
ifdef CONFIG_SYS_SUPPORTS_ZBOOT
	$(Q)install -D -m 755 vmlinuz $(INSTALL_PATH)/vmlinuz-$(KERNELRELEASE)
endif
	$(Q)install -D -m 644 .config $(INSTALL_PATH)/config-$(KERNELRELEASE)
	$(Q)install -D -m 644 System.map $(INSTALL_PATH)/System.map-$(KERNELRELEASE)

archclean:
	$(Q)$(MAKE) $(clean)=arch/mips/boot
	$(Q)$(MAKE) $(clean)=arch/mips/boot/compressed
	$(Q)$(MAKE) $(clean)=arch/mips/boot/tools
	$(Q)$(MAKE) $(clean)=arch/mips/lasat

define archhelp
	echo '  install              - install kernel into $(INSTALL_PATH)'
	echo '  vmlinux.ecoff        - ECOFF boot image'
	echo '  vmlinux.bin          - Raw binary boot image'
	echo '  vmlinux.srec         - SREC boot image'
	echo '  vmlinux.32           - 64-bit boot image wrapped in 32bits (IP22/IP32)'
	echo '  vmlinuz              - Compressed boot(zboot) image'
	echo '  vmlinuz.ecoff        - ECOFF zboot image'
	echo '  vmlinuz.bin          - Raw binary zboot image'
	echo '  vmlinuz.srec         - SREC zboot image'
	echo '  uImage               - U-Boot image'
	echo '  uImage.bin           - U-Boot image (uncompressed)'
	echo '  uImage.bz2           - U-Boot image (bz2)'
	echo '  uImage.gz            - U-Boot image (gzip)'
	echo '  uImage.lzma          - U-Boot image (lzma)'
	echo '  uImage.lzo           - U-Boot image (lzo)'
	echo '  dtbs                 - Device-tree blobs for enabled boards'
	echo '  dtbs_install         - Install dtbs to $(INSTALL_DTBS_PATH)'
	echo
	echo '  These will be default as appropriate for a configured platform.'
	echo
	echo '  If you are targeting a system supported by generic kernels you may'
	echo '  configure the kernel for a given architecture target like so:'
	echo
	echo '  {micro32,32,64}{r1,r2,r6}{el,}_defconfig <BOARDS="list of boards">'
	echo
	echo '  Otherwise, the following default configurations are available:'
endef

generic_config_dir = $(srctree)/arch/$(ARCH)/configs/generic
generic_defconfigs :=

#
# If the user generates a generic kernel configuration without specifying a
# list of boards to include the config fragments for, default to including all
# available board config fragments.
#
ifeq ($(BOARDS),)
BOARDS = $(patsubst board-%.config,%,$(notdir $(wildcard $(generic_config_dir)/board-*.config)))
endif

#
# Generic kernel configurations which merge generic_defconfig with the
# appropriate config fragments from arch/mips/configs/generic/, resulting in
# the ability to easily configure the kernel for a given architecture,
# endianness & set of boards without duplicating the needed configuration in
# hundreds of defconfig files.
#
define gen_generic_defconfigs
$(foreach bits,$(1),$(foreach rev,$(2),$(foreach endian,$(3),
target := $(bits)$(rev)$(filter el,$(endian))_defconfig
generic_defconfigs += $$(target)
$$(target): $(generic_config_dir)/$(bits)$(rev).config
$$(target): $(generic_config_dir)/$(endian).config
)))
endef

$(eval $(call gen_generic_defconfigs,32 64,r1 r2 r6,eb el))
$(eval $(call gen_generic_defconfigs,micro32,r2,eb el))

.PHONY: $(generic_defconfigs)
$(generic_defconfigs):
	$(Q)$(CONFIG_SHELL) $(srctree)/scripts/kconfig/merge_config.sh \
		-m -O $(objtree) $(srctree)/arch/$(ARCH)/configs/generic_defconfig $^ \
		$(foreach board,$(BOARDS),$(generic_config_dir)/board-$(board).config)
	$(Q)$(MAKE) olddefconfig

#
# Prevent generic merge_config rules attempting to merge single fragments
#
$(generic_config_dir)/%.config: ;

#
# Legacy defconfig compatibility - these targets used to be real defconfigs but
# now that the boards have been converted to use the generic kernel they are
# wrappers around the generic rules above.
#
.PHONY: sead3_defconfig
sead3_defconfig:
	$(Q)$(MAKE) 32r2el_defconfig BOARDS=sead-3

.PHONY: sead3micro_defconfig
sead3micro_defconfig:
	$(Q)$(MAKE) micro32r2el_defconfig BOARDS=sead-3
