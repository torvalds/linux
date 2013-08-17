#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

GZIP = gzip
GZIP_FLAGS = -v9fc

ifeq ($(BIG_ENDIAN),1)
OBJCOPY_ARGS    := -O elf32-xtensa-be
else
OBJCOPY_ARGS    := -O elf32-xtensa-le
endif

export OBJCOPY_ARGS
export CPPFLAGS_boot.lds += -P -C

boot-y		:= bootstrap.o

OBJS		:= $(addprefix $(obj)/,$(boot-y))

Image:	vmlinux $(OBJS) arch/$(ARCH)/boot/boot-elf/boot.lds
	$(OBJCOPY) --strip-all -R .comment -R .note.gnu.build-id -O binary \
		vmlinux vmlinux.tmp
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section image=vmlinux.tmp \
		--set-section-flags image=contents,alloc,load,load,data \
		$(OBJS) $@.tmp
	$(LD) $(LDFLAGS) $(LDFLAGS_vmlinux) \
		-T arch/$(ARCH)/boot/boot-elf/boot.lds \
		-o arch/$(ARCH)/boot/$@.elf $@.tmp
	rm -f $@.tmp vmlinux.tmp

Image.initrd:	vmlinux $(OBJS)
	$(OBJCOPY) --strip-all -R .comment -R .note.gnu.build-id -O binary \
		--add-section .initrd=arch/$(ARCH)/boot/ramdisk \
		--set-section-flags .initrd=contents,alloc,load,load,data \
		vmlinux vmlinux.tmp
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section image=vmlinux.tmp \
		--set-section-flags image=contents,alloc,load,load,data \
		$(OBJS) $@.tmp
	$(LD) $(LDFLAGS) $(LDFLAGS_vmlinux) \
		-T $(srctree)/arch/$(ARCH)/boot/boot-elf/boot.ld \
		-o arch/$(ARCH)/boot/$@.elf $@.tmp
	rm -f $@.tmp vmlinux.tmp


zImage:	Image

zImage.initrd:	Image.initrd

