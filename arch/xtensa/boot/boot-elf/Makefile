#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

ifeq ($(BIG_ENDIAN),1)
OBJCOPY_ARGS    := -O elf32-xtensa-be
else
OBJCOPY_ARGS    := -O elf32-xtensa-le
endif

export OBJCOPY_ARGS
export CPPFLAGS_boot.lds += -P -C

boot-y		:= bootstrap.o

OBJS		:= $(addprefix $(obj)/,$(boot-y))

$(obj)/Image.o: vmlinux.bin $(OBJS)
	$(Q)$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section image=vmlinux.bin \
		--set-section-flags image=contents,alloc,load,load,data \
		$(OBJS) $@

$(obj)/../Image.elf: $(obj)/Image.o $(obj)/boot.lds
	$(Q)$(LD) $(LDFLAGS) $(LDFLAGS_vmlinux) \
		-T $(obj)/boot.lds \
		--build-id=none \
		-o $@ $(obj)/Image.o
	$(Q)$(kecho) '  Kernel: $@ is ready'

zImage:	$(obj)/../Image.elf
