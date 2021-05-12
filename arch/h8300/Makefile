#
# arch/h8300/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# (C) Copyright 2002-2015 Yoshinori Sato <ysato@users.sourceforge.jp>
#

KBUILD_DEFCONFIG := edosk2674_defconfig

cflags-$(CONFIG_CPU_H8300H)	:= -mh
aflags-$(CONFIG_CPU_H8300H)	:= -mh -Wa,--mach=h8300h
ldflags-$(CONFIG_CPU_H8300H)	:= -mh8300helf_linux
cflags-$(CONFIG_CPU_H8S)	:= -ms
aflags-$(CONFIG_CPU_H8S)	:= -ms -Wa,--mach=h8300s
ldflags-$(CONFIG_CPU_H8S)	:= -mh8300self_linux

KBUILD_CFLAGS += $(cflags-y)
KBUILD_CFLAGS += -mint32 -fno-builtin
KBUILD_CFLAGS += -D__linux__
KBUILD_CFLAGS += -DUTS_SYSNAME=\"uClinux\"
KBUILD_AFLAGS += $(aflags-y)
KBUILD_LDFLAGS += $(ldflags-y)

CHECKFLAGS += -msize-long

ifeq ($(CROSS_COMPILE),)
CROSS_COMPILE := $(call cc-cross-prefix, h8300-unknown-linux- h8300-linux-)
endif

libs-y	+= arch/$(ARCH)/lib/

boot := arch/h8300/boot

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

vmlinux.srec vmlinux.bin zImage uImage.bin: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

define archhelp
  @echo  'vmlinux.bin  - Create raw binary'
  @echo  'vmlinux.srec - Create srec binary'
  @echo  'zImage       - Compressed kernel image'
endef
