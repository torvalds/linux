#
# arch/h8300/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# (C) Copyright 2002,2003 Yoshinori Sato <ysato@users.sourceforge.jp>
#

platform-$(CONFIG_CPU_H8300H)	:= h8300h
platform-$(CONFIG_CPU_H8S)	:= h8s
PLATFORM := $(platform-y)

board-$(CONFIG_H8300H_GENERIC)		:= generic
board-$(CONFIG_H8300H_AKI3068NET)	:= aki3068net
board-$(CONFIG_H8300H_H8MAX)		:= h8max
board-$(CONFIG_H8300H_SIM)		:= generic
board-$(CONFIG_H8S_GENERIC)		:= generic
board-$(CONFIG_H8S_EDOSK2674)		:= edosk2674
board-$(CONFIG_H8S_SIM)			:= generic
BOARD := $(board-y)

model-$(CONFIG_RAMKERNEL)	:= ram
model-$(CONFIG_ROMKERNEL)	:= rom
MODEL := $(model-y)

cflags-$(CONFIG_CPU_H8300H)	:= -mh
ldflags-$(CONFIG_CPU_H8300H)	:= -mh8300helf
cflags-$(CONFIG_CPU_H8S)	:= -ms
ldflags-$(CONFIG_CPU_H8S)	:= -mh8300self

CFLAGS += $(cflags-y)
CFLAGS += -mint32 -fno-builtin
CFLAGS += -g
CFLAGS += -D__linux__
CFLAGS += -DUTS_SYSNAME=\"uClinux\"
AFLAGS += -DPLATFORM=$(PLATFORM) -DMODEL=$(MODEL) $(cflags-y)
LDFLAGS += $(ldflags-y)

CROSS_COMPILE = h8300-elf-
LIBGCC := $(shell $(CROSS-COMPILE)$(CC) $(CFLAGS) -print-libgcc-file-name)

head-y := arch/$(ARCH)/platform/$(platform-y)/$(board-y)/crt0_$(model-y).o

core-y	+= arch/$(ARCH)/kernel/ \
	   arch/$(ARCH)/mm/
ifdef PLATFORM
core-y	+= arch/$(ARCH)/platform/$(PLATFORM)/ \
	   arch/$(ARCH)/platform/$(PLATFORM)/$(BOARD)/
endif

libs-y	+= arch/$(ARCH)/lib/ $(LIBGCC)

boot := arch/h8300/boot

export MODEL PLATFORM BOARD

archmrproper:

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

vmlinux.srec vmlinux.bin: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

define archhelp
  echo  'vmlinux.bin  - Create raw binary'
  echo  'vmlinux.srec - Create srec binary'
endef
