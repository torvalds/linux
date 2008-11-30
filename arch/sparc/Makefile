#
# sparc/Makefile
#
# Makefile for the architecture dependent flags and dependencies on the
# Sparc.
#
# Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
#

#
# Uncomment the first KBUILD_CFLAGS if you are doing kgdb source level
# debugging of the kernel to get the proper debugging information.

AS             := $(AS) -32
LDFLAGS        := -m elf32_sparc
CHECKFLAGS     += -D__sparc__
export BITS    := 32

#KBUILD_CFLAGS += -g -pipe -fcall-used-g5 -fcall-used-g7
KBUILD_CFLAGS += -m32 -pipe -mno-fpu -fcall-used-g5 -fcall-used-g7
KBUILD_AFLAGS += -m32
CPPFLAGS_vmlinux.lds += -m32

#LDFLAGS_vmlinux = -N -Ttext 0xf0004000
#  Since 2.5.40, the first stage is left not btfix-ed.
#  Actual linking is done with "make image".
LDFLAGS_vmlinux = -r

head-y := arch/sparc/kernel/head.o arch/sparc/kernel/init_task.o

core-y += arch/sparc/kernel/ arch/sparc/mm/ arch/sparc/math-emu/
libs-y += arch/sparc/prom/ arch/sparc/lib/

drivers-$(CONFIG_OPROFILE)	+= arch/sparc/oprofile/

# Export what is needed by arch/sparc/boot/Makefile
export VMLINUX_INIT VMLINUX_MAIN
VMLINUX_INIT := $(head-y) $(init-y)
VMLINUX_MAIN := $(core-y) kernel/ mm/ fs/ ipc/ security/ crypto/ block/
VMLINUX_MAIN += $(patsubst %/, %/lib.a, $(libs-y)) $(libs-y)
VMLINUX_MAIN += $(drivers-y) $(net-y)

ifdef CONFIG_KALLSYMS
export kallsyms.o := .tmp_kallsyms2.o
endif

# Default target
all: zImage

boot := arch/sparc/boot

image zImage tftpboot.img: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

# This is the image used for packaging
KBUILD_IMAGE := $(boot)/zImage

# Don't use tabs in echo arguments.
define archhelp
  echo  '* image        - kernel image ($(boot)/image)'
  echo  '* zImage       - stripped kernel image ($(boot)/zImage)'
  echo  '  tftpboot.img - image prepared for tftp'
endef
