# sparc64/Makefile
#
# Makefile for the architecture dependent flags and dependencies on the
# 64-bit Sparc.
#
# Copyright (C) 1996,1998 David S. Miller (davem@caip.rutgers.edu)
# Copyright (C) 1998 Jakub Jelinek (jj@ultra.linux.cz)
#

CHECKFLAGS	+= -D__sparc__ -D__sparc_v9__ -D__arch64__ -m64

# Undefine sparc when processing vmlinux.lds - it is used
# And teach CPP we are doing 64 bit builds (for this case)
CPPFLAGS_vmlinux.lds += -m64 -Usparc

LDFLAGS		:= -m elf64_sparc

KBUILD_CFLAGS += -m64 -pipe -mno-fpu -mcpu=ultrasparc -mcmodel=medlow \
	-ffixed-g4 -ffixed-g5 -fcall-used-g7 -Wno-sign-compare \
	-Wa,--undeclared-regs
KBUILD_CFLAGS += $(call cc-option,-mtune=ultrasparc3)
KBUILD_AFLAGS += -m64 -mcpu=ultrasparc -Wa,--undeclared-regs

ifeq ($(CONFIG_MCOUNT),y)
  KBUILD_CFLAGS += -pg
endif

head-y := arch/sparc64/kernel/head.o arch/sparc64/kernel/init_task.o

core-y				+= arch/sparc64/kernel/ arch/sparc64/mm/
core-y				+= arch/sparc64/math-emu/
libs-y				+= arch/sparc64/prom/ arch/sparc64/lib/
drivers-$(CONFIG_OPROFILE)	+= arch/sparc64/oprofile/

boot := arch/sparc64/boot

image tftpboot.img vmlinux.aout: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

define archhelp
  echo  '* vmlinux       - Standard sparc64 kernel'
  echo  '  vmlinux.aout  - a.out kernel for sparc64'
  echo  '  tftpboot.img  - Image prepared for tftp'
endef

