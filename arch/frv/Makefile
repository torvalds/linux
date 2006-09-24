#
# frv/Makefile
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
# Copyright (c) 2003, 2004 Red Hat Inc.
# - Written by David Howells <dhowells@redhat.com>
# - Derived from arch/m68knommu/Makefile,
#	Copyright (c) 1999,2001  D. Jeff Dionne <jeff@lineo.ca>,
#	Rt-Control Inc. / Lineo, Inc.
#
# Copyright (C) 1998,1999  D. Jeff Dionne <jeff@uclinux.org>,
#                          Kenneth Albanowski <kjahds@kjahds.com>,
#
# Based on arch/m68k/Makefile:
# Copyright (C) 1994 by Hamish Macdonald
#

CCSPECS	:= $(shell $(CC) -v 2>&1 | grep "^Reading specs from " | head -1 | cut -c20-)
CCDIR	:= $(strip $(patsubst %/specs,%,$(CCSPECS)))
CPUCLASS := fr400

# test for cross compiling
COMPILE_ARCH = $(shell uname -m)

ifdef CONFIG_MMU
UTS_SYSNAME = -DUTS_SYSNAME=\"Linux\"
else
UTS_SYSNAME = -DUTS_SYSNAME=\"uClinux\"
endif

ARCHMODFLAGS	+= -G0 -mlong-calls

ifdef CONFIG_GPREL_DATA_8
CFLAGS		+= -G8
else
ifdef CONFIG_GPREL_DATA_4
CFLAGS		+= -G4
else
ifdef CONFIG_GPREL_DATA_NONE
CFLAGS		+= -G0
endif
endif
endif

#LDFLAGS_vmlinux	:= -Map linkmap.txt

ifdef CONFIG_GC_SECTIONS
CFLAGS		+= -ffunction-sections -fdata-sections
LINKFLAGS	+= --gc-sections
endif

ifndef CONFIG_FRAME_POINTER
CFLAGS		+= -mno-linked-fp
endif

ifdef CONFIG_CPU_FR451_COMPILE
CFLAGS		+= -mcpu=fr450
AFLAGS		+= -mcpu=fr450
ASFLAGS		+= -mcpu=fr450
else
ifdef CONFIG_CPU_FR551_COMPILE
CFLAGS		+= -mcpu=fr550
AFLAGS		+= -mcpu=fr550
ASFLAGS		+= -mcpu=fr550
else
CFLAGS		+= -mcpu=fr400
AFLAGS		+= -mcpu=fr400
ASFLAGS		+= -mcpu=fr400
endif
endif

# pretend the kernel is going to run on an FR400 with no media-fp unit
# - reserve CC3 for use with atomic ops
# - all the extra registers are dealt with only at context switch time
CFLAGS		+= -mno-fdpic -mgpr-32 -msoft-float -mno-media
CFLAGS		+= -ffixed-fcc3 -ffixed-cc3 -ffixed-gr15 -ffixed-icc2
AFLAGS		+= -mno-fdpic
ASFLAGS		+= -mno-fdpic

# make sure the .S files get compiled with debug info
# and disable optimisations that are unhelpful whilst debugging
ifdef CONFIG_DEBUG_INFO
CFLAGS		+= -O1
AFLAGS		+= -Wa,--gdwarf2
ASFLAGS		+= -Wa,--gdwarf2
endif

head-y		:= arch/frv/kernel/head.o arch/frv/kernel/init_task.o

core-y		+= arch/frv/kernel/ arch/frv/mm/
libs-y		+= arch/frv/lib/

core-$(CONFIG_MB93090_MB00)	+= arch/frv/mb93090-mb00/

all: Image

Image: vmlinux
	$(Q)$(MAKE) $(build)=arch/frv/boot $@

bootstrap:
	$(Q)$(MAKEBOOT) bootstrap

archclean:
	$(Q)$(MAKE) $(clean)=arch/frv/boot

archdep: scripts/mkdep symlinks
	$(Q)$(MAKE) $(build)=arch/frv/boot dep
