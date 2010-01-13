#
# This file is subject to the terms and conditions of the GNU General Public
# License.
#
# Adapted for MIPS Pete Popov, Dan Malek
#
# Copyright (C) 1994 by Linus Torvalds
# Adapted for PowerPC by Gary Thomas
# modified by Cort (cort@cs.nmt.edu)
#
# Copyright (C) 2009 Lemote Inc. & DSLab, Lanzhou University
# Author: Wu Zhangjin <wuzhangjin@gmail.com>
#

# compressed kernel load addr: VMLINUZ_LOAD_ADDRESS > VMLINUX_LOAD_ADDRESS + VMLINUX_SIZE
VMLINUX_SIZE := $(shell wc -c $(objtree)/$(KBUILD_IMAGE) 2>/dev/null | cut -d' ' -f1)
VMLINUX_SIZE := $(shell [ -n "$(VMLINUX_SIZE)" ] && echo -n $$(($(VMLINUX_SIZE) + (65536 - $(VMLINUX_SIZE) % 65536))))
# VMLINUZ_LOAD_ADDRESS = concat "high32 of VMLINUX_LOAD_ADDRESS" and "(low32 of VMLINUX_LOAD_ADDRESS) + VMLINUX_SIZE"
HIGH32 := $(shell A=$(VMLINUX_LOAD_ADDRESS); [ $${\#A} -gt 10 ] && expr substr "$(VMLINUX_LOAD_ADDRESS)" 3 $$(($${\#A} - 10)))
LOW32 := $(shell [ -n "$(HIGH32)" ] && A=11 || A=3; expr substr "$(VMLINUX_LOAD_ADDRESS)" $${A} 8)
VMLINUZ_LOAD_ADDRESS := 0x$(shell [ -n "$(VMLINUX_SIZE)" -a -n "$(LOW32)" ] && printf "$(HIGH32)%08x" $$(($(VMLINUX_SIZE) + 0x$(LOW32))))

# set the default size of the mallocing area for decompressing
BOOT_HEAP_SIZE := 0x400000

# Disable Function Tracer
KBUILD_CFLAGS := $(shell echo $(KBUILD_CFLAGS) | sed -e "s/-pg//")

KBUILD_CFLAGS := $(LINUXINCLUDE) $(KBUILD_CFLAGS) -D__KERNEL__ \
	-DBOOT_HEAP_SIZE=$(BOOT_HEAP_SIZE) -D"VMLINUX_LOAD_ADDRESS_ULL=$(VMLINUX_LOAD_ADDRESS)ull"

KBUILD_AFLAGS := $(LINUXINCLUDE) $(KBUILD_AFLAGS) -D__ASSEMBLY__ \
	-DBOOT_HEAP_SIZE=$(BOOT_HEAP_SIZE) \
	-DKERNEL_ENTRY=0x$(shell $(NM) $(objtree)/$(KBUILD_IMAGE) 2>/dev/null | grep " kernel_entry" | cut -f1 -d \ )

obj-y := $(obj)/head.o $(obj)/decompress.o $(obj)/dbg.o

obj-$(CONFIG_SYS_SUPPORTS_ZBOOT_UART16550) += $(obj)/uart-16550.o
obj-$(CONFIG_MACH_ALCHEMY)		   += $(obj)/uart-alchemy.o

OBJCOPYFLAGS_vmlinux.bin := $(OBJCOPYFLAGS) -O binary -R .comment -S
$(obj)/vmlinux.bin: $(KBUILD_IMAGE)
	$(call if_changed,objcopy)

suffix_$(CONFIG_KERNEL_GZIP)  = gz
suffix_$(CONFIG_KERNEL_BZIP2) = bz2
suffix_$(CONFIG_KERNEL_LZMA)  = lzma
suffix_$(CONFIG_KERNEL_LZO)   = lzo
tool_$(CONFIG_KERNEL_GZIP)    = gzip
tool_$(CONFIG_KERNEL_BZIP2)   = bzip2
tool_$(CONFIG_KERNEL_LZMA)    = lzma
tool_$(CONFIG_KERNEL_LZO)     = lzo
$(obj)/vmlinux.$(suffix_y): $(obj)/vmlinux.bin
	$(call if_changed,$(tool_y))

$(obj)/piggy.o: $(obj)/vmlinux.$(suffix_y) $(obj)/dummy.o
	$(Q)$(OBJCOPY) $(OBJCOPYFLAGS) \
		--add-section=.image=$< \
		--set-section-flags=.image=contents,alloc,load,readonly,data \
		$(obj)/dummy.o $@

LDFLAGS_vmlinuz := $(LDFLAGS) -Ttext $(VMLINUZ_LOAD_ADDRESS) -T
vmlinuz: $(src)/ld.script $(obj-y) $(obj)/piggy.o
	$(call if_changed,ld)
	$(Q)$(OBJCOPY) $(OBJCOPYFLAGS) $@

#
# Some DECstations need all possible sections of an ECOFF executable
#
ifdef CONFIG_MACH_DECSTATION
  E2EFLAGS = -a
else
  E2EFLAGS =
endif

# elf2ecoff can only handle 32bit image

ifdef CONFIG_32BIT
	VMLINUZ = vmlinuz
else
	VMLINUZ = vmlinuz.32
endif

vmlinuz.32: vmlinuz
	$(Q)$(OBJCOPY) -O $(32bit-bfd) $(OBJCOPYFLAGS) $< $@

vmlinuz.ecoff: $(obj)/../elf2ecoff $(VMLINUZ)
	$(Q)$(obj)/../elf2ecoff $(VMLINUZ) vmlinuz.ecoff $(E2EFLAGS)

$(obj)/../elf2ecoff: $(src)/../elf2ecoff.c
	$(Q)$(HOSTCC) -o $@ $^

OBJCOPYFLAGS_vmlinuz.bin := $(OBJCOPYFLAGS) -O binary
vmlinuz.bin: vmlinuz
	$(call if_changed,objcopy)

OBJCOPYFLAGS_vmlinuz.srec := $(OBJCOPYFLAGS) -S -O srec
vmlinuz.srec: vmlinuz
	$(call if_changed,objcopy)

clean:
clean-files += *.o \
	       vmlinu*
