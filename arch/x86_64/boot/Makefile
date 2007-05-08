#
# arch/x86_64/boot/Makefile
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

targets		:= vmlinux.bin bootsect bootsect.o \
		   setup setup.o bzImage mtools.conf

EXTRA_CFLAGS := -m32

hostprogs-y	:= tools/build
HOST_EXTRACFLAGS += $(LINUXINCLUDE)
subdir-		:= compressed/	#Let make clean descend in compressed/
# ---------------------------------------------------------------------------

$(obj)/bzImage: IMAGE_OFFSET := 0x100000
$(obj)/bzImage: EXTRA_AFLAGS := $(SVGA_MODE) $(RAMDISK) -D__BIG_KERNEL__
$(obj)/bzImage: BUILDFLAGS   := -b

quiet_cmd_image = BUILD   $@
cmd_image = $(obj)/tools/build $(BUILDFLAGS) $(obj)/bootsect $(obj)/setup \
	    $(obj)/vmlinux.bin $(ROOT_DEV) > $@

$(obj)/bzImage: $(obj)/bootsect $(obj)/setup \
			      $(obj)/vmlinux.bin $(obj)/tools/build FORCE
	$(call if_changed,image)
	@echo 'Kernel: $@ is ready' ' (#'`cat .version`')'

$(obj)/vmlinux.bin: $(obj)/compressed/vmlinux FORCE
	$(call if_changed,objcopy)

LDFLAGS_bootsect := -Ttext 0x0 -s --oformat binary
LDFLAGS_setup	 := -Ttext 0x0 -s --oformat binary -e begtext

$(obj)/setup $(obj)/bootsect: %: %.o FORCE
	$(call if_changed,ld)

$(obj)/compressed/vmlinux: FORCE
	$(Q)$(MAKE) $(build)=$(obj)/compressed IMAGE_OFFSET=$(IMAGE_OFFSET) $@

# Set this if you want to pass append arguments to the zdisk/fdimage/isoimage kernel
FDARGS = 
# Set this if you want an initrd included with the zdisk/fdimage/isoimage kernel
FDINITRD =

image_cmdline = default linux $(FDARGS) $(if $(FDINITRD),initrd=initrd.img,)

$(obj)/mtools.conf: $(src)/mtools.conf.in
	sed -e 's|@OBJ@|$(obj)|g' < $< > $@

# This requires write access to /dev/fd0
zdisk: $(BOOTIMAGE) $(obj)/mtools.conf
	MTOOLSRC=$(obj)/mtools.conf mformat a:			; sync
	syslinux /dev/fd0					; sync
	echo '$(image_cmdline)' | \
		MTOOLSRC=$(obj)/mtools.conf mcopy - a:syslinux.cfg
	if [ -f '$(FDINITRD)' ] ; then \
		MTOOLSRC=$(obj)/mtools.conf mcopy '$(FDINITRD)' a:initrd.img ; \
	fi
	MTOOLSRC=$(obj)/mtools.conf mcopy $(BOOTIMAGE) a:linux	; sync

# These require being root or having syslinux 2.02 or higher installed
fdimage fdimage144: $(BOOTIMAGE) $(obj)/mtools.conf
	dd if=/dev/zero of=$(obj)/fdimage bs=1024 count=1440
	MTOOLSRC=$(obj)/mtools.conf mformat v:			; sync
	syslinux $(obj)/fdimage					; sync
	echo '$(image_cmdline)' | \
		MTOOLSRC=$(obj)/mtools.conf mcopy - v:syslinux.cfg
	if [ -f '$(FDINITRD)' ] ; then \
		MTOOLSRC=$(obj)/mtools.conf mcopy '$(FDINITRD)' v:initrd.img ; \
	fi
	MTOOLSRC=$(obj)/mtools.conf mcopy $(BOOTIMAGE) v:linux	; sync

fdimage288: $(BOOTIMAGE) $(obj)/mtools.conf
	dd if=/dev/zero of=$(obj)/fdimage bs=1024 count=2880
	MTOOLSRC=$(obj)/mtools.conf mformat w:			; sync
	syslinux $(obj)/fdimage					; sync
	echo '$(image_cmdline)' | \
		MTOOLSRC=$(obj)/mtools.conf mcopy - w:syslinux.cfg
	if [ -f '$(FDINITRD)' ] ; then \
		MTOOLSRC=$(obj)/mtools.conf mcopy '$(FDINITRD)' w:initrd.img ; \
	fi
	MTOOLSRC=$(obj)/mtools.conf mcopy $(BOOTIMAGE) w:linux	; sync

isoimage: $(BOOTIMAGE)
	-rm -rf $(obj)/isoimage
	mkdir $(obj)/isoimage
	for i in lib lib64 share end ; do \
		if [ -f /usr/$$i/syslinux/isolinux.bin ] ; then \
			cp /usr/$$i/syslinux/isolinux.bin $(obj)/isoimage ; \
			break ; \
		fi ; \
		if [ $$i = end ] ; then exit 1 ; fi ; \
	done
	cp $(BOOTIMAGE) $(obj)/isoimage/linux
	echo '$(image_cmdline)' > $(obj)/isoimage/isolinux.cfg
	if [ -f '$(FDINITRD)' ] ; then \
		cp '$(FDINITRD)' $(obj)/isoimage/initrd.img ; \
	fi
	mkisofs -J -r -o $(obj)/image.iso -b isolinux.bin -c boot.cat \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		$(obj)/isoimage
	rm -rf $(obj)/isoimage

zlilo: $(BOOTIMAGE)
	if [ -f $(INSTALL_PATH)/vmlinuz ]; then mv $(INSTALL_PATH)/vmlinuz $(INSTALL_PATH)/vmlinuz.old; fi
	if [ -f $(INSTALL_PATH)/System.map ]; then mv $(INSTALL_PATH)/System.map $(INSTALL_PATH)/System.old; fi
	cat $(BOOTIMAGE) > $(INSTALL_PATH)/vmlinuz
	cp System.map $(INSTALL_PATH)/
	if [ -x /sbin/lilo ]; then /sbin/lilo; else /etc/lilo/install; fi

install:
	sh $(srctree)/$(src)/install.sh $(KERNELRELEASE) $(BOOTIMAGE) System.map "$(INSTALL_PATH)"
