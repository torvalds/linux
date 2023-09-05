#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

OBJCOPY_ARGS := -O $(if $(CONFIG_CPU_BIG_ENDIAN),elf32-xtensa-be,elf32-xtensa-le)

boot-y	:= bootstrap.o
targets	+= $(boot-y)

OBJS	:= $(addprefix $(obj)/,$(boot-y))
LIBS	:= arch/xtensa/boot/lib/lib.a arch/xtensa/lib/lib.a

$(obj)/zImage.o: $(obj)/../vmlinux.bin.gz $(OBJS)
	$(Q)$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section image=$< \
		--set-section-flags image=contents,alloc,load,load,data \
		$(OBJS) $@

$(obj)/zImage.elf: $(obj)/zImage.o $(LIBS)
	$(Q)$(LD) $(KBUILD_LDFLAGS) \
		-T $(srctree)/$(obj)/boot.ld \
		--build-id=none \
		-o $@ $^

$(obj)/../zImage.redboot: $(obj)/zImage.elf
	$(Q)$(OBJCOPY) -S -O binary $< $@
	$(Q)$(kecho) '  Kernel: $@ is ready'

all zImage: $(obj)/../zImage.redboot
