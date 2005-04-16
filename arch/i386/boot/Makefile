#
# arch/i386/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
#

# ROOT_DEV specifies the default root-device when making the image.
# This can be either FLOPPY, CURRENT, /dev/xxxx or empty, in which case
# the default of FLOPPY is used by 'build'.

ROOT_DEV := CURRENT

# If you want to preset the SVGA mode, uncomment the next line and
# set SVGA_MODE to whatever number you want.
# Set it to -DSVGA_MODE=NORMAL_VGA if you just want the EGA/VGA mode.
# The number is the same as you would ordinarily press at bootup.

SVGA_MODE := -DSVGA_MODE=NORMAL_VGA

# If you want the RAM disk device, define this to be the size in blocks.

#RAMDISK := -DRAMDISK=512

targets		:= vmlinux.bin bootsect bootsect.o setup setup.o \
		   zImage bzImage
subdir- 	:= compressed

hostprogs-y	:= tools/build

HOSTCFLAGS_build.o := $(LINUXINCLUDE)

# ---------------------------------------------------------------------------

$(obj)/zImage:  IMAGE_OFFSET := 0x1000
$(obj)/zImage:  EXTRA_AFLAGS := -traditional $(SVGA_MODE) $(RAMDISK)
$(obj)/bzImage: IMAGE_OFFSET := 0x100000
$(obj)/bzImage: EXTRA_AFLAGS := -traditional $(SVGA_MODE) $(RAMDISK) -D__BIG_KERNEL__
$(obj)/bzImage: BUILDFLAGS   := -b

quiet_cmd_image = BUILD   $@
cmd_image = $(obj)/tools/build $(BUILDFLAGS) $(obj)/bootsect $(obj)/setup \
	    $(obj)/vmlinux.bin $(ROOT_DEV) > $@

$(obj)/zImage $(obj)/bzImage: $(obj)/bootsect $(obj)/setup \
			      $(obj)/vmlinux.bin $(obj)/tools/build FORCE
	$(call if_changed,image)
	@echo 'Kernel: $@ is ready'

$(obj)/vmlinux.bin: $(obj)/compressed/vmlinux FORCE
	$(call if_changed,objcopy)

LDFLAGS_bootsect := -Ttext 0x0 -s --oformat binary
LDFLAGS_setup	 := -Ttext 0x0 -s --oformat binary -e begtext

$(obj)/setup $(obj)/bootsect: %: %.o FORCE
	$(call if_changed,ld)

$(obj)/compressed/vmlinux: FORCE
	$(Q)$(MAKE) $(build)=$(obj)/compressed IMAGE_OFFSET=$(IMAGE_OFFSET) $@

# Set this if you want to pass append arguments to the zdisk/fdimage kernel
FDARGS = 

$(obj)/mtools.conf: $(src)/mtools.conf.in
	sed -e 's|@OBJ@|$(obj)|g' < $< > $@

# This requires write access to /dev/fd0
zdisk: $(BOOTIMAGE) $(obj)/mtools.conf
	MTOOLSRC=$(obj)/mtools.conf mformat a:			; sync
	syslinux /dev/fd0					; sync
	echo 'default linux $(FDARGS)' | \
		MTOOLSRC=$(src)/mtools.conf mcopy - a:syslinux.cfg
	MTOOLSRC=$(obj)/mtools.conf mcopy $(BOOTIMAGE) a:linux	; sync

# These require being root or having syslinux 2.02 or higher installed
fdimage fdimage144: $(BOOTIMAGE) $(obj)/mtools.conf
	dd if=/dev/zero of=$(obj)/fdimage bs=1024 count=1440
	MTOOLSRC=$(obj)/mtools.conf mformat v:			; sync
	syslinux $(obj)/fdimage					; sync
	echo 'default linux $(FDARGS)' | \
		MTOOLSRC=$(obj)/mtools.conf mcopy - v:syslinux.cfg
	MTOOLSRC=$(obj)/mtools.conf mcopy $(BOOTIMAGE) v:linux	; sync

fdimage288: $(BOOTIMAGE) $(obj)/mtools.conf
	dd if=/dev/zero of=$(obj)/fdimage bs=1024 count=2880
	MTOOLSRC=$(obj)/mtools.conf mformat w:			; sync
	syslinux $(obj)/fdimage					; sync
	echo 'default linux $(FDARGS)' | \
		MTOOLSRC=$(obj)/mtools.conf mcopy - w:syslinux.cfg
	MTOOLSRC=$(obj)/mtools.conf mcopy $(BOOTIMAGE) w:linux	; sync

zlilo: $(BOOTIMAGE)
	if [ -f $(INSTALL_PATH)/vmlinuz ]; then mv $(INSTALL_PATH)/vmlinuz $(INSTALL_PATH)/vmlinuz.old; fi
	if [ -f $(INSTALL_PATH)/System.map ]; then mv $(INSTALL_PATH)/System.map $(INSTALL_PATH)/System.old; fi
	cat $(BOOTIMAGE) > $(INSTALL_PATH)/vmlinuz
	cp System.map $(INSTALL_PATH)/
	if [ -x /sbin/lilo ]; then /sbin/lilo; else /etc/lilo/install; fi

install: $(BOOTIMAGE)
	sh $(srctree)/$(src)/install.sh $(KERNELRELEASE) $< System.map "$(INSTALL_PATH)"
