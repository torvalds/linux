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

KBUILD_DEFCONFIG := ip22_defconfig

cflags-y := -ffunction-sections

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
MODFLAGS			+= -mlong-calls

cflags-y += -ffreestanding

#
# We explicitly add the endianness specifier if needed, this allows
# to compile kernels with a toolchain for the other endianness. We
# carefully avoid to add it redundantly because gcc 3.3/3.4 complains
# when fed the toolchain default!
#
# Certain gcc versions upto gcc 4.1.1 (probably 4.2-subversion as of
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

cflags-$(CONFIG_CPU_HAS_SMARTMIPS)	+= $(call cc-option,-msmartmips)

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
cflags-$(CONFIG_CPU_LOONGSON2)	+= -march=r4600 -Wa,--trap
cflags-$(CONFIG_CPU_MIPS32_R1)	+= $(call cc-option,-march=mips32,-mips32 -U_MIPS_ISA -D_MIPS_ISA=_MIPS_ISA_MIPS32) \
			-Wa,-mips32 -Wa,--trap
cflags-$(CONFIG_CPU_MIPS32_R2)	+= $(call cc-option,-march=mips32r2,-mips32r2 -U_MIPS_ISA -D_MIPS_ISA=_MIPS_ISA_MIPS32) \
			-Wa,-mips32r2 -Wa,--trap
cflags-$(CONFIG_CPU_MIPS64_R1)	+= $(call cc-option,-march=mips64,-mips64 -U_MIPS_ISA -D_MIPS_ISA=_MIPS_ISA_MIPS64) \
			-Wa,-mips64 -Wa,--trap
cflags-$(CONFIG_CPU_MIPS64_R2)	+= $(call cc-option,-march=mips64r2,-mips64r2 -U_MIPS_ISA -D_MIPS_ISA=_MIPS_ISA_MIPS64) \
			-Wa,-mips64r2 -Wa,--trap
cflags-$(CONFIG_CPU_R5000)	+= -march=r5000 -Wa,--trap
cflags-$(CONFIG_CPU_R5432)	+= $(call cc-option,-march=r5400,-march=r5000) \
			-Wa,--trap
cflags-$(CONFIG_CPU_NEVADA)	+= $(call cc-option,-march=rm5200,-march=r5000) \
			-Wa,--trap
cflags-$(CONFIG_CPU_RM7000)	+= $(call cc-option,-march=rm7000,-march=r5000) \
			-Wa,--trap
cflags-$(CONFIG_CPU_RM9000)	+= $(call cc-option,-march=rm9000,-march=r5000) \
			-Wa,--trap
cflags-$(CONFIG_CPU_SB1)	+= $(call cc-option,-march=sb1,-march=r5000) \
			-Wa,--trap
cflags-$(CONFIG_CPU_R8000)	+= -march=r8000 -Wa,--trap
cflags-$(CONFIG_CPU_R10000)	+= $(call cc-option,-march=r10000,-march=r8000) \
			-Wa,--trap

cflags-$(CONFIG_CPU_R4000_WORKAROUNDS)	+= $(call cc-option,-mfix-r4000,)
cflags-$(CONFIG_CPU_R4400_WORKAROUNDS)	+= $(call cc-option,-mfix-r4400,)
cflags-$(CONFIG_CPU_DADDI_WORKAROUNDS)	+= $(call cc-option,-mno-daddi,)

ifdef CONFIG_CPU_SB1
ifdef CONFIG_SB1_PASS_1_WORKAROUNDS
MODFLAGS	+= -msb1-pass1-workarounds
endif
endif

#
# Firmware support
#
libs-$(CONFIG_ARC)		+= arch/mips/fw/arc/
libs-$(CONFIG_CFE)		+= arch/mips/fw/cfe/
libs-$(CONFIG_SNIPROM)		+= arch/mips/fw/sni/
libs-y				+= arch/mips/fw/lib/
libs-$(CONFIG_SIBYTE_CFE)	+= arch/mips/sibyte/cfe/

#
# Board-dependent options and extra files
#

#
# Acer PICA 61, Mips Magnum 4000 and Olivetti M700.
#
core-$(CONFIG_MACH_JAZZ)	+= arch/mips/jazz/
cflags-$(CONFIG_MACH_JAZZ)	+= -Iinclude/asm-mips/mach-jazz
load-$(CONFIG_MACH_JAZZ)	+= 0xffffffff80080000

#
# Common Alchemy Au1x00 stuff
#
core-$(CONFIG_SOC_AU1X00)	+= arch/mips/au1000/common/
cflags-$(CONFIG_SOC_AU1X00)	+= -Iinclude/asm-mips/mach-au1x00

#
# AMD Alchemy Pb1000 eval board
#
libs-$(CONFIG_MIPS_PB1000)	+= arch/mips/au1000/pb1000/
cflags-$(CONFIG_MIPS_PB1000)	+= -Iinclude/asm-mips/mach-pb1x00
load-$(CONFIG_MIPS_PB1000)	+= 0xffffffff80100000

#
# AMD Alchemy Pb1100 eval board
#
libs-$(CONFIG_MIPS_PB1100)	+= arch/mips/au1000/pb1100/
cflags-$(CONFIG_MIPS_PB1100)	+= -Iinclude/asm-mips/mach-pb1x00
load-$(CONFIG_MIPS_PB1100)	+= 0xffffffff80100000

#
# AMD Alchemy Pb1500 eval board
#
libs-$(CONFIG_MIPS_PB1500)	+= arch/mips/au1000/pb1500/
cflags-$(CONFIG_MIPS_PB1500)	+= -Iinclude/asm-mips/mach-pb1x00
load-$(CONFIG_MIPS_PB1500)	+= 0xffffffff80100000

#
# AMD Alchemy Pb1550 eval board
#
libs-$(CONFIG_MIPS_PB1550)	+= arch/mips/au1000/pb1550/
cflags-$(CONFIG_MIPS_PB1550)	+= -Iinclude/asm-mips/mach-pb1x00
load-$(CONFIG_MIPS_PB1550)	+= 0xffffffff80100000

#
# AMD Alchemy Pb1200 eval board
#
libs-$(CONFIG_MIPS_PB1200)	+= arch/mips/au1000/pb1200/
cflags-$(CONFIG_MIPS_PB1200)	+= -Iinclude/asm-mips/mach-pb1x00
load-$(CONFIG_MIPS_PB1200)	+= 0xffffffff80100000

#
# AMD Alchemy Db1000 eval board
#
libs-$(CONFIG_MIPS_DB1000)	+= arch/mips/au1000/db1x00/
cflags-$(CONFIG_MIPS_DB1000)	+= -Iinclude/asm-mips/mach-db1x00
load-$(CONFIG_MIPS_DB1000)	+= 0xffffffff80100000

#
# AMD Alchemy Db1100 eval board
#
libs-$(CONFIG_MIPS_DB1100)	+= arch/mips/au1000/db1x00/
cflags-$(CONFIG_MIPS_DB1100)	+= -Iinclude/asm-mips/mach-db1x00
load-$(CONFIG_MIPS_DB1100)	+= 0xffffffff80100000

#
# AMD Alchemy Db1500 eval board
#
libs-$(CONFIG_MIPS_DB1500)	+= arch/mips/au1000/db1x00/
cflags-$(CONFIG_MIPS_DB1500)	+= -Iinclude/asm-mips/mach-db1x00
load-$(CONFIG_MIPS_DB1500)	+= 0xffffffff80100000

#
# AMD Alchemy Db1550 eval board
#
libs-$(CONFIG_MIPS_DB1550)	+= arch/mips/au1000/db1x00/
cflags-$(CONFIG_MIPS_DB1550)	+= -Iinclude/asm-mips/mach-db1x00
load-$(CONFIG_MIPS_DB1550)	+= 0xffffffff80100000

#
# AMD Alchemy Db1200 eval board
#
libs-$(CONFIG_MIPS_DB1200)	+= arch/mips/au1000/pb1200/
cflags-$(CONFIG_MIPS_DB1200)	+= -Iinclude/asm-mips/mach-db1x00
load-$(CONFIG_MIPS_DB1200)	+= 0xffffffff80100000

#
# AMD Alchemy Bosporus eval board
#
libs-$(CONFIG_MIPS_BOSPORUS)	+= arch/mips/au1000/db1x00/
cflags-$(CONFIG_MIPS_BOSPORUS)	+= -Iinclude/asm-mips/mach-db1x00
load-$(CONFIG_MIPS_BOSPORUS)	+= 0xffffffff80100000

#
# AMD Alchemy Mirage eval board
#
libs-$(CONFIG_MIPS_MIRAGE)	+= arch/mips/au1000/db1x00/
cflags-$(CONFIG_MIPS_MIRAGE)	+= -Iinclude/asm-mips/mach-db1x00
load-$(CONFIG_MIPS_MIRAGE)	+= 0xffffffff80100000

#
# 4G-Systems eval board
#
libs-$(CONFIG_MIPS_MTX1)	+= arch/mips/au1000/mtx-1/
load-$(CONFIG_MIPS_MTX1)	+= 0xffffffff80100000

#
# MyCable eval board
#
libs-$(CONFIG_MIPS_XXS1500)	+= arch/mips/au1000/xxs1500/
load-$(CONFIG_MIPS_XXS1500)	+= 0xffffffff80100000

#
# Cobalt Server
#
core-$(CONFIG_MIPS_COBALT)	+= arch/mips/cobalt/
cflags-$(CONFIG_MIPS_COBALT)	+= -Iinclude/asm-mips/mach-cobalt
load-$(CONFIG_MIPS_COBALT)	+= 0xffffffff80080000

#
# DECstation family
#
core-$(CONFIG_MACH_DECSTATION)	+= arch/mips/dec/
cflags-$(CONFIG_MACH_DECSTATION)+= -Iinclude/asm-mips/mach-dec
libs-$(CONFIG_MACH_DECSTATION)	+= arch/mips/dec/prom/
load-$(CONFIG_MACH_DECSTATION)	+= 0xffffffff80040000

#
# Wind River PPMC Board (4KC + GT64120)
#
core-$(CONFIG_WR_PPMC)		+= arch/mips/gt64120/wrppmc/
cflags-$(CONFIG_WR_PPMC)		+= -Iinclude/asm-mips/mach-wrppmc
load-$(CONFIG_WR_PPMC)		+= 0xffffffff80100000

#
# lemote fulong mini-PC board
#
core-$(CONFIG_LEMOTE_FULONG) +=arch/mips/lemote/lm2e/
load-$(CONFIG_LEMOTE_FULONG) +=0xffffffff80100000
cflags-$(CONFIG_LEMOTE_FULONG) += -Iinclude/asm-mips/mach-lemote

#
# MIPS Malta board
#
core-$(CONFIG_MIPS_MALTA)	+= arch/mips/mti-malta/
cflags-$(CONFIG_MIPS_MALTA)	+= -Iinclude/asm-mips/mach-malta
load-$(CONFIG_MIPS_MALTA)	+= 0xffffffff80100000
all-$(CONFIG_MIPS_MALTA)	:= vmlinux.bin

#
# MIPS SIM
#
core-$(CONFIG_MIPS_SIM)		+= arch/mips/mipssim/
cflags-$(CONFIG_MIPS_SIM)	+= -Iinclude/asm-mips/mach-mipssim
load-$(CONFIG_MIPS_SIM)		+= 0x80100000

#
# PMC-Sierra MSP SOCs
#
core-$(CONFIG_PMC_MSP)		+= arch/mips/pmc-sierra/msp71xx/
cflags-$(CONFIG_PMC_MSP)	+= -Iinclude/asm-mips/pmc-sierra/msp71xx \
					-mno-branch-likely
load-$(CONFIG_PMC_MSP)		+= 0xffffffff80100000

#
# PMC-Sierra Yosemite
#
core-$(CONFIG_PMC_YOSEMITE)	+= arch/mips/pmc-sierra/yosemite/
cflags-$(CONFIG_PMC_YOSEMITE)	+= -Iinclude/asm-mips/mach-yosemite
load-$(CONFIG_PMC_YOSEMITE)	+= 0xffffffff80100000

#
# Basler eXcite
#
core-$(CONFIG_BASLER_EXCITE)	+= arch/mips/basler/excite/
cflags-$(CONFIG_BASLER_EXCITE)	+= -Iinclude/asm-mips/mach-excite
load-$(CONFIG_BASLER_EXCITE)	+= 0x80100000

#
# LASAT platforms
#
core-$(CONFIG_LASAT)		+= arch/mips/lasat/
cflags-$(CONFIG_LASAT)		+= -Iinclude/asm-mips/mach-lasat
load-$(CONFIG_LASAT)		+= 0xffffffff80000000

#
# Common VR41xx
#
core-$(CONFIG_MACH_VR41XX)	+= arch/mips/vr41xx/common/
cflags-$(CONFIG_MACH_VR41XX)	+= -Iinclude/asm-mips/mach-vr41xx

#
# ZAO Networks Capcella (VR4131)
#
load-$(CONFIG_ZAO_CAPCELLA)	+= 0xffffffff80000000

#
# Victor MP-C303/304 (VR4122)
#
load-$(CONFIG_VICTOR_MPC30X)	+= 0xffffffff80001000

#
# IBM WorkPad z50 (VR4121)
#
core-$(CONFIG_IBM_WORKPAD)	+= arch/mips/vr41xx/ibm-workpad/
load-$(CONFIG_IBM_WORKPAD)	+= 0xffffffff80004000

#
# CASIO CASSIPEIA E-55/65 (VR4111)
#
core-$(CONFIG_CASIO_E55)	+= arch/mips/vr41xx/casio-e55/
load-$(CONFIG_CASIO_E55)	+= 0xffffffff80004000

#
# TANBAC VR4131 multichip module(TB0225) and TANBAC VR4131DIMM(TB0229) (VR4131)
#
load-$(CONFIG_TANBAC_TB022X)	+= 0xffffffff80000000

#
# Common NXP PNX8550
#
core-$(CONFIG_SOC_PNX8550)	+= arch/mips/nxp/pnx8550/common/
cflags-$(CONFIG_SOC_PNX8550)	+= -Iinclude/asm-mips/mach-pnx8550

#
# NXP PNX8550 JBS board
#
libs-$(CONFIG_PNX8550_JBS)	+= arch/mips/nxp/pnx8550/jbs/
#cflags-$(CONFIG_PNX8550_JBS)	+= -Iinclude/asm-mips/mach-pnx8550
load-$(CONFIG_PNX8550_JBS)	+= 0xffffffff80060000

# NXP PNX8550 STB810 board
#
libs-$(CONFIG_PNX8550_STB810)	+= arch/mips/nxp/pnx8550/stb810/
load-$(CONFIG_PNX8550_STB810)	+= 0xffffffff80060000

# NEC EMMA2RH boards
#
core-$(CONFIG_EMMA2RH)          += arch/mips/emma2rh/common/
cflags-$(CONFIG_EMMA2RH)        += -Iinclude/asm-mips/mach-emma2rh

# NEC EMMA2RH Mark-eins
core-$(CONFIG_MARKEINS)         += arch/mips/emma2rh/markeins/
load-$(CONFIG_MARKEINS)         += 0xffffffff88100000

#
# SGI IP22 (Indy/Indigo2)
#
# Set the load address to >= 0xffffffff88069000 if you want to leave space for
# symmon, 0xffffffff80002000 for production kernels.  Note that the value must
# be aligned to a multiple of the kernel stack size or the handling of the
# current variable will break so for 64-bit kernels we have to raise the start
# address by 8kb.
#
core-$(CONFIG_SGI_IP22)		+= arch/mips/sgi-ip22/
cflags-$(CONFIG_SGI_IP22)	+= -Iinclude/asm-mips/mach-ip22
ifdef CONFIG_32BIT
load-$(CONFIG_SGI_IP22)		+= 0xffffffff88002000
endif
ifdef CONFIG_64BIT
load-$(CONFIG_SGI_IP22)		+= 0xffffffff88004000
endif

#
# SGI-IP27 (Origin200/2000)
#
# Set the load address to >= 0xc000000000300000 if you want to leave space for
# symmon, 0xc00000000001c000 for production kernels.  Note that the value must
# be 16kb aligned or the handling of the current variable will break.
#
ifdef CONFIG_SGI_IP27
core-$(CONFIG_SGI_IP27)		+= arch/mips/sgi-ip27/
cflags-$(CONFIG_SGI_IP27)	+= -Iinclude/asm-mips/mach-ip27
ifdef CONFIG_MAPPED_KERNEL
load-$(CONFIG_SGI_IP27)		+= 0xc00000004001c000
OBJCOPYFLAGS			:= --change-addresses=0x3fffffff80000000
dataoffset-$(CONFIG_SGI_IP27)	+= 0x01000000
else
load-$(CONFIG_SGI_IP27)		+= 0xa80000000001c000
OBJCOPYFLAGS			:= --change-addresses=0x57ffffff80000000
endif
endif

#
# SGI IP28 (Indigo2 R10k)
#
# Set the load address to >= 0xa800000020080000 if you want to leave space for
# symmon, 0xa800000020004000 for production kernels ?  Note that the value must
# be 16kb aligned or the handling of the current variable will break.
# Simplified: what IP22 does at 128MB+ in ksegN, IP28 does at 512MB+ in xkphys
#
ifdef CONFIG_SGI_IP28
  ifeq ($(call cc-option-yn,-mr10k-cache-barrier=1), n)
      $(error gcc doesn't support needed option -mr10k-cache-barrier=1)
  endif
endif
core-$(CONFIG_SGI_IP28)		+= arch/mips/sgi-ip22/
cflags-$(CONFIG_SGI_IP28)	+= -mr10k-cache-barrier=1 -Iinclude/asm-mips/mach-ip28
load-$(CONFIG_SGI_IP28)		+= 0xa800000020004000

#
# SGI-IP32 (O2)
#
# Set the load address to >= 80069000 if you want to leave space for symmon,
# 0xffffffff80004000 for production kernels.  Note that the value must be aligned to
# a multiple of the kernel stack size or the handling of the current variable
# will break.
#
core-$(CONFIG_SGI_IP32)		+= arch/mips/sgi-ip32/
cflags-$(CONFIG_SGI_IP32)	+= -Iinclude/asm-mips/mach-ip32
load-$(CONFIG_SGI_IP32)		+= 0xffffffff80004000

#
# Sibyte SB1250/BCM1480 SOC
#
# This is a LIB so that it links at the end, and initcalls are later
# the sequence; but it is built as an object so that modules don't get
# removed (as happens, even if they have __initcall/module_init)
#
core-$(CONFIG_SIBYTE_BCM112X)	+= arch/mips/sibyte/sb1250/
core-$(CONFIG_SIBYTE_BCM112X)	+= arch/mips/sibyte/common/
cflags-$(CONFIG_SIBYTE_BCM112X)	+= -Iinclude/asm-mips/mach-sibyte \
			-DSIBYTE_HDR_FEATURES=SIBYTE_HDR_FMASK_1250_112x_ALL

core-$(CONFIG_SIBYTE_SB1250)	+= arch/mips/sibyte/sb1250/
core-$(CONFIG_SIBYTE_SB1250)	+= arch/mips/sibyte/common/
cflags-$(CONFIG_SIBYTE_SB1250)	+= -Iinclude/asm-mips/mach-sibyte \
			-DSIBYTE_HDR_FEATURES=SIBYTE_HDR_FMASK_1250_112x_ALL

core-$(CONFIG_SIBYTE_BCM1x55)	+= arch/mips/sibyte/bcm1480/
core-$(CONFIG_SIBYTE_BCM1x55)	+= arch/mips/sibyte/common/
cflags-$(CONFIG_SIBYTE_BCM1x55)	+= -Iinclude/asm-mips/mach-sibyte \
			-DSIBYTE_HDR_FEATURES=SIBYTE_HDR_FMASK_1480_ALL

core-$(CONFIG_SIBYTE_BCM1x80)	+= arch/mips/sibyte/bcm1480/
core-$(CONFIG_SIBYTE_BCM1x80)	+= arch/mips/sibyte/common/
cflags-$(CONFIG_SIBYTE_BCM1x80)	+= -Iinclude/asm-mips/mach-sibyte \
			-DSIBYTE_HDR_FEATURES=SIBYTE_HDR_FMASK_1480_ALL

#
# Sibyte BCM91120x (Carmel) board
# Sibyte BCM91120C (CRhine) board
# Sibyte BCM91125C (CRhone) board
# Sibyte BCM91125E (Rhone) board
# Sibyte SWARM board
# Sibyte BCM91x80 (BigSur) board
#
core-$(CONFIG_SIBYTE_CARMEL)	+= arch/mips/sibyte/swarm/
load-$(CONFIG_SIBYTE_CARMEL)	:= 0xffffffff80100000
core-$(CONFIG_SIBYTE_CRHINE)	+= arch/mips/sibyte/swarm/
load-$(CONFIG_SIBYTE_CRHINE)	:= 0xffffffff80100000
core-$(CONFIG_SIBYTE_CRHONE)	+= arch/mips/sibyte/swarm/
load-$(CONFIG_SIBYTE_CRHONE)	:= 0xffffffff80100000
core-$(CONFIG_SIBYTE_RHONE)	+= arch/mips/sibyte/swarm/
load-$(CONFIG_SIBYTE_RHONE)	:= 0xffffffff80100000
core-$(CONFIG_SIBYTE_SENTOSA)	+= arch/mips/sibyte/swarm/
load-$(CONFIG_SIBYTE_SENTOSA)	:= 0xffffffff80100000
core-$(CONFIG_SIBYTE_SWARM)	+= arch/mips/sibyte/swarm/
load-$(CONFIG_SIBYTE_SWARM)	:= 0xffffffff80100000
core-$(CONFIG_SIBYTE_BIGSUR)	+= arch/mips/sibyte/swarm/
load-$(CONFIG_SIBYTE_BIGSUR)	:= 0xffffffff80100000

#
# Broadcom BCM47XX boards
#
core-$(CONFIG_BCM47XX)		+= arch/mips/bcm47xx/
cflags-$(CONFIG_BCM47XX)	+= -Iinclude/asm-mips/mach-bcm47xx
load-$(CONFIG_BCM47XX)		:= 0xffffffff80001000

#
# SNI RM
#
core-$(CONFIG_SNI_RM)		+= arch/mips/sni/
cflags-$(CONFIG_SNI_RM)		+= -Iinclude/asm-mips/mach-rm
ifdef CONFIG_CPU_LITTLE_ENDIAN
load-$(CONFIG_SNI_RM)		+= 0xffffffff80600000
else
load-$(CONFIG_SNI_RM)		+= 0xffffffff80030000
endif
all-$(CONFIG_SNI_RM)		:= vmlinux.ecoff

#
# Common TXx9
#
core-$(CONFIG_MACH_TX39XX)	+= arch/mips/txx9/generic/
cflags-$(CONFIG_MACH_TX39XX) += -Iinclude/asm-mips/mach-tx39xx
load-$(CONFIG_MACH_TX39XX)	+= 0xffffffff80050000
core-$(CONFIG_MACH_TX49XX)	+= arch/mips/txx9/generic/
cflags-$(CONFIG_MACH_TX49XX) += -Iinclude/asm-mips/mach-tx49xx
load-$(CONFIG_MACH_TX49XX)	+= 0xffffffff80100000

#
# Toshiba JMR-TX3927 board
#
core-$(CONFIG_TOSHIBA_JMR3927)	+= arch/mips/txx9/jmr3927/

#
# Routerboard 532 board
#
core-$(CONFIG_MIKROTIK_RB532)	+= arch/mips/rb532/
cflags-$(CONFIG_MIKROTIK_RB532) += -Iinclude/asm-mips/mach-rc32434
load-$(CONFIG_MIKROTIK_RB532)	+= 0xffffffff80101000

#
# Toshiba RBTX4927 board or
# Toshiba RBTX4937 board
#
core-$(CONFIG_TOSHIBA_RBTX4927)	+= arch/mips/txx9/rbtx4927/

#
# Toshiba RBTX4938 board
#
core-$(CONFIG_TOSHIBA_RBTX4938) += arch/mips/txx9/rbtx4938/

cflags-y			+= -Iinclude/asm-mips/mach-generic
drivers-$(CONFIG_PCI)		+= arch/mips/pci/

ifdef CONFIG_32BIT
ifdef CONFIG_CPU_LITTLE_ENDIAN
JIFFIES			= jiffies_64
else
JIFFIES			= jiffies_64 + 4
endif
else
JIFFIES			= jiffies_64
endif

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
KBUILD_CFLAGS	+= $(cflags-y) \
			-D"VMLINUX_LOAD_ADDRESS=$(load-y)"

LDFLAGS			+= -m $(ld-emul)

ifdef CONFIG_MIPS
CHECKFLAGS += $(shell $(CC) $(KBUILD_CFLAGS) -dM -E -xc /dev/null | \
	egrep -vw '__GNUC_(|MINOR_|PATCHLEVEL_)_' | \
	sed -e 's/^\#define /-D/' -e "s/ /='/" -e "s/$$/'/")
ifdef CONFIG_64BIT
CHECKFLAGS		+= -m64
endif
endif

OBJCOPYFLAGS		+= --remove-section=.reginfo

#
# Choosing incompatible machines durings configuration will result in
# error messages during linking.  Select a default linkscript if
# none has been choosen above.
#

CPPFLAGS_vmlinux.lds := \
	$(KBUILD_CFLAGS) \
	-D"LOADADDR=$(load-y)" \
	-D"JIFFIES=$(JIFFIES)" \
	-D"DATAOFFSET=$(if $(dataoffset-y),$(dataoffset-y),0)"

head-y := arch/mips/kernel/head.o arch/mips/kernel/init_task.o

libs-y			+= arch/mips/lib/

core-y			+= arch/mips/kernel/ arch/mips/mm/ arch/mips/math-emu/

drivers-$(CONFIG_OPROFILE)	+= arch/mips/oprofile/

ifdef CONFIG_LASAT
rom.bin rom.sw: vmlinux
	$(Q)$(MAKE) $(build)=arch/mips/lasat/image $@
endif

#
# Some machines like the Indy need 32-bit ELF binaries for booting purposes.
# Other need ECOFF, so we build a 32-bit ELF binary for them which we then
# convert to ECOFF using elf2ecoff.
#
vmlinux.32: vmlinux
	$(OBJCOPY) -O $(32bit-bfd) $(OBJCOPYFLAGS) $< $@

#
# The 64-bit ELF tools are pretty broken so at this time we generate 64-bit
# ELF files from 32-bit files by conversion.
#
vmlinux.64: vmlinux
	$(OBJCOPY) -O $(64bit-bfd) $(OBJCOPYFLAGS) $< $@

makeboot =$(Q)$(MAKE) $(build)=arch/mips/boot VMLINUX=$(vmlinux-32) $(1)

all:	$(all-y)

vmlinux.bin: $(vmlinux-32)
	+@$(call makeboot,$@)

vmlinux.ecoff: $(vmlinux-32)
	+@$(call makeboot,$@)

vmlinux.srec: $(vmlinux-32)
	+@$(call makeboot,$@)

CLEAN_FILES += vmlinux.ecoff \
	       vmlinux.srec

archprepare:
ifdef CONFIG_MIPS32_N32
	@echo '  Checking missing-syscalls for N32'
	$(Q)$(MAKE) $(build)=. missing-syscalls EXTRA_CFLAGS="-mabi=n32"
endif
ifdef CONFIG_MIPS32_O32
	@echo '  Checking missing-syscalls for O32'
	$(Q)$(MAKE) $(build)=. missing-syscalls EXTRA_CFLAGS="-mabi=32"
endif

archclean:
	@$(MAKE) $(clean)=arch/mips/boot
	@$(MAKE) $(clean)=arch/mips/lasat

define archhelp
	echo '  vmlinux.ecoff        - ECOFF boot image'
	echo '  vmlinux.bin          - Raw binary boot image'
	echo '  vmlinux.srec         - SREC boot image'
	echo
	echo '  These will be default as apropriate for a configured platform.'
endef

CLEAN_FILES += vmlinux.32 \
	       vmlinux.64 \
	       vmlinux.ecoff
