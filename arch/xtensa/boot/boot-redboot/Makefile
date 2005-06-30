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

LIBGCC := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

zImage: vmlinux $(OBJS) $(LIBS)
	$(OBJCOPY) --strip-all -R .comment -R .xt.insn -O binary \
		vmlinux vmlinux.tmp
	gzip -vf9 vmlinux.tmp
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section image=vmlinux.tmp.gz \
		--set-section-flags image=contents,alloc,load,load,data \
		$(OBJS) $@.tmp
	$(LD) $(LD_ARGS) -o $@.elf $@.tmp $(LIBS) -L/xtensa-elf/lib $(LIBGCC)
	$(OBJCOPY) -S -O binary $@.elf arch/$(ARCH)/boot/$@.redboot
	rm -f $@.tmp $@.elf vmlinux.tmp.gz

