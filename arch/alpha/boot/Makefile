#
# arch/alpha/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
#

hostprogs-y	:= tools/mkbb tools/objstrip
targets		:= vmlinux.gz vmlinux \
		   vmlinux.nh tools/lxboot tools/bootlx tools/bootph \
		   tools/bootpzh bootloader bootpheader bootpzheader 
OBJSTRIP	:= $(obj)/tools/objstrip

HOSTCFLAGS	:= -Wall -I$(objtree)/usr/include
BOOTCFLAGS	+= -I$(obj) -I$(srctree)/$(obj)

# SRM bootable image.  Copy to offset 512 of a partition.
$(obj)/bootimage: $(addprefix $(obj)/tools/,mkbb lxboot bootlx) $(obj)/vmlinux.nh
	( cat $(obj)/tools/lxboot $(obj)/tools/bootlx $(obj)/vmlinux.nh ) > $@ 
	$(obj)/tools/mkbb $@ $(obj)/tools/lxboot
	@echo '  Bootimage $@ is ready'

# BOOTP bootable image.  Define INITRD during make to append initrd image.
$(obj)/bootpfile: $(obj)/tools/bootph $(obj)/vmlinux.nh
	cat $(obj)/tools/bootph $(obj)/vmlinux.nh > $@
ifdef INITRD
	cat $(INITRD) >> $@
endif

# Compressed kernel BOOTP bootable image.
# Define INITRD during make to append initrd image.
$(obj)/bootpzfile: $(obj)/tools/bootpzh $(obj)/vmlinux.nh.gz
	cat $(obj)/tools/bootpzh $(obj)/vmlinux.nh.gz > $@
ifdef INITRD
	cat $(INITRD) >> $@
endif

# Compressed kernel image
$(obj)/vmlinux.gz: $(obj)/vmlinux FORCE
	$(call if_changed,gzip)
	@echo '  Kernel $@ is ready'

$(obj)/main.o: $(obj)/ksize.h
$(obj)/bootp.o: $(obj)/ksize.h
$(obj)/bootpz.o: $(obj)/kzsize.h

$(obj)/ksize.h: $(obj)/vmlinux.nh FORCE
	echo "#define KERNEL_SIZE `ls -l $(obj)/vmlinux.nh | awk '{print $$5}'`" > $@T
ifdef INITRD
	[ -f $(INITRD) ] || exit 1
	echo "#define INITRD_IMAGE_SIZE `ls -l $(INITRD) | awk '{print $$5}'`" >> $@T
endif
	cmp -s $@T $@ || mv -f $@T $@
	rm -f $@T

$(obj)/kzsize.h: $(obj)/vmlinux.nh.gz FORCE
	echo "#define KERNEL_SIZE `ls -l $(obj)/vmlinux.nh | awk '{print $$5}'`" > $@T
	echo "#define KERNEL_Z_SIZE `ls -l $(obj)/vmlinux.nh.gz | awk '{print $$5}'`" >> $@T
ifdef INITRD
	[ -f $(INITRD) ] || exit 1
	echo "#define INITRD_IMAGE_SIZE `ls -l $(INITRD) | awk '{print $$5}'`" >> $@T
endif
	cmp -s $@T $@ || mv -f $@T $@
	rm -f $@T

quiet_cmd_strip = STRIP  $@
      cmd_strip = $(STRIP) -o $@ $<

$(obj)/vmlinux: vmlinux FORCE
	$(call if_changed,strip)

quiet_cmd_objstrip = OBJSTRIP $@
      cmd_objstrip = $(OBJSTRIP) $(OSFLAGS_$(@F)) $< $@

OSFLAGS_vmlinux.nh	:= -v
OSFLAGS_lxboot		:= -p
OSFLAGS_bootlx		:= -vb
OSFLAGS_bootph		:= -vb
OSFLAGS_bootpzh		:= -vb

$(obj)/vmlinux.nh: vmlinux $(OBJSTRIP) FORCE
	$(call if_changed,objstrip)

$(obj)/vmlinux.nh.gz: $(obj)/vmlinux.nh FORCE
	$(call if_changed,gzip)

$(obj)/tools/lxboot: $(obj)/bootloader $(OBJSTRIP) FORCE
	$(call if_changed,objstrip)

$(obj)/tools/bootlx: $(obj)/bootloader $(OBJSTRIP) FORCE
	$(call if_changed,objstrip)

$(obj)/tools/bootph: $(obj)/bootpheader $(OBJSTRIP) FORCE
	$(call if_changed,objstrip)

$(obj)/tools/bootpzh: $(obj)/bootpzheader $(OBJSTRIP) FORCE
	$(call if_changed,objstrip)

LDFLAGS_bootloader   := -static -T # -N -relax
LDFLAGS_bootloader   := -static -T # -N -relax
LDFLAGS_bootpheader  := -static -T # -N -relax
LDFLAGS_bootpzheader := -static -T # -N -relax

OBJ_bootlx   := $(obj)/head.o $(obj)/stdio.o $(obj)/main.o
OBJ_bootph   := $(obj)/head.o $(obj)/stdio.o $(obj)/bootp.o
OBJ_bootpzh  := $(obj)/head.o $(obj)/stdio.o $(obj)/bootpz.o $(obj)/misc.o

$(obj)/bootloader: $(obj)/bootloader.lds $(OBJ_bootlx) $(LIBS_Y) FORCE
	$(call if_changed,ld)

$(obj)/bootpheader: $(obj)/bootloader.lds $(OBJ_bootph) $(LIBS_Y) FORCE
	$(call if_changed,ld)

$(obj)/bootpzheader: $(obj)/bootloader.lds $(OBJ_bootpzh) $(LIBS_Y) FORCE
	$(call if_changed,ld)

$(obj)/misc.o: lib/inflate.c
