#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

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

$(obj)/zImage.o: vmlinux.bin.gz $(OBJS)
	$(Q)$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section image=vmlinux.bin.gz \
		--set-section-flags image=contents,alloc,load,load,data \
		$(OBJS) $@

$(obj)/zImage.elf: $(obj)/zImage.o $(LIBS)
	$(Q)$(LD) $(LD_ARGS) -o $@ $^ -L/xtensa-elf/lib $(LIBGCC)

$(obj)/../zImage.redboot: $(obj)/zImage.elf
	$(Q)$(OBJCOPY) -S -O binary $< $@
	$(Q)$(kecho) '  Kernel: $@ is ready'

zImage: $(obj)/../zImage.redboot
