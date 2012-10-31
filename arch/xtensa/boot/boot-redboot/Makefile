#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

GZIP = gzip
GZIP_FLAGS = -v9fc
ifeq ($(BIG_ENDIAN),1)
OBJCOPY_ARGS 	:= -O elf32-xtensa-be
else
OBJCOPY_ARGS 	:= -O elf32-xtensa-le
endif

LD_ARGS	= -T $(srctree)/$(obj)/boot.ld

boot-y	:= bootstrap.o

OBJS	:= $(addprefix $(obj)/,$(boot-y))
LIBS	:= arch/xtensa/boot/lib/lib.a arch/xtensa/lib/lib.a

LIBGCC := $(shell $(CC) $(KBUILD_CFLAGS) -print-libgcc-file-name)

vmlinux.tmp: vmlinux
	$(OBJCOPY) --strip-all -R .comment -R .note.gnu.build-id -O binary \
		$^ $@

vmlinux.tmp.gz: vmlinux.tmp
	$(GZIP) $(GZIP_FLAGS) $^ > $@

zImage: vmlinux.tmp.gz $(OBJS) $(LIBS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section image=vmlinux.tmp.gz \
		--set-section-flags image=contents,alloc,load,load,data \
		$(OBJS) $@.tmp
	$(LD) $(LD_ARGS) -o $@.elf $@.tmp $(LIBS) -L/xtensa-elf/lib $(LIBGCC)
	$(OBJCOPY) -S -O binary $@.elf arch/$(ARCH)/boot/$@.redboot
