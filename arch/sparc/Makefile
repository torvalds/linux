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

AS              := $(AS) -32
LDFLAGS		:= -m elf32_sparc
CHECKFLAGS	+= -D__sparc__

#KBUILD_CFLAGS += -g -pipe -fcall-used-g5 -fcall-used-g7
KBUILD_CFLAGS += -m32 -pipe -mno-fpu -fcall-used-g5 -fcall-used-g7
KBUILD_AFLAGS += -m32
CPPFLAGS_vmlinux.lds += -m32

#LDFLAGS_vmlinux = -N -Ttext 0xf0004000
#  Since 2.5.40, the first stage is left not btfix-ed.
#  Actual linking is done with "make image".
LDFLAGS_vmlinux = -r

head-y := arch/sparc/kernel/head.o arch/sparc/kernel/init_task.o
HEAD_Y := $(head-y)

core-y += arch/sparc/kernel/ arch/sparc/mm/ arch/sparc/math-emu/
libs-y += arch/sparc/prom/ arch/sparc/lib/

drivers-$(CONFIG_OPROFILE)	+= arch/sparc/oprofile/

# Export what is needed by arch/sparc/boot/Makefile
# Renaming is done to avoid confusing pattern matching rules in 2.5.45 (multy-)
INIT_Y		:= $(patsubst %/, %/built-in.o, $(init-y))
CORE_Y		:= $(core-y)
CORE_Y		+= kernel/ mm/ fs/ ipc/ security/ crypto/ block/
CORE_Y		:= $(patsubst %/, %/built-in.o, $(CORE_Y))
DRIVERS_Y	:= $(patsubst %/, %/built-in.o, $(drivers-y))
NET_Y		:= $(patsubst %/, %/built-in.o, $(net-y))
LIBS_Y1		:= $(patsubst %/, %/lib.a, $(libs-y))
LIBS_Y2		:= $(patsubst %/, %/built-in.o, $(libs-y))
LIBS_Y		:= $(LIBS_Y1) $(LIBS_Y2)

ifdef CONFIG_KALLSYMS
kallsyms.o := .tmp_kallsyms2.o
endif

export INIT_Y CORE_Y DRIVERS_Y NET_Y LIBS_Y HEAD_Y kallsyms.o

# Default target
all: zImage

boot := arch/sparc/boot

image zImage tftpboot.img: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

# This is the image used for packaging
KBUILD_IMAGE := $(boot)/zImage

CLEAN_FILES += arch/$(ARCH)/boot/System.map

# Don't use tabs in echo arguments.
define archhelp
  echo  '* image        - kernel image ($(boot)/image)'
  echo  '* zImage       - stripped kernel image ($(boot)/zImage)'
  echo  '  tftpboot.img - image prepared for tftp'
endef
