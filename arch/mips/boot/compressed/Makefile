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

include $(srctree)/arch/mips/Kbuild.platforms

# set the default size of the mallocing area for decompressing
BOOT_HEAP_SIZE := 0x400000

# Disable Function Tracer
KBUILD_CFLAGS := $(filter-out $(CC_FLAGS_FTRACE), $(KBUILD_CFLAGS))

KBUILD_CFLAGS := $(filter-out -fstack-protector, $(KBUILD_CFLAGS))

# Disable lq/sq in zboot
ifdef CONFIG_CPU_LOONGSON64
KBUILD_CFLAGS := $(filter-out -march=loongson3a, $(KBUILD_CFLAGS)) -march=mips64r2
endif

KBUILD_CFLAGS := $(KBUILD_CFLAGS) -D__KERNEL__ -D__DISABLE_EXPORTS \
	-DBOOT_HEAP_SIZE=$(BOOT_HEAP_SIZE) -D"VMLINUX_LOAD_ADDRESS_ULL=$(VMLINUX_LOAD_ADDRESS)ull"

KBUILD_AFLAGS := $(KBUILD_AFLAGS) -D__ASSEMBLY__ \
	-DBOOT_HEAP_SIZE=$(BOOT_HEAP_SIZE) \
	-DKERNEL_ENTRY=$(VMLINUX_ENTRY_ADDRESS)

# Prevents link failures: __sanitizer_cov_trace_pc() is not linked in.
KCOV_INSTRUMENT		:= n
GCOV_PROFILE := n
UBSAN_SANITIZE := n

# decompressor objects (linked with vmlinuz)
vmlinuzobjs-y := $(obj)/head.o $(obj)/decompress.o $(obj)/string.o $(obj)/bswapsi.o

ifdef CONFIG_DEBUG_ZBOOT
vmlinuzobjs-$(CONFIG_DEBUG_ZBOOT)		   += $(obj)/dbg.o
vmlinuzobjs-$(CONFIG_SYS_SUPPORTS_ZBOOT_UART16550) += $(obj)/uart-16550.o
vmlinuzobjs-$(CONFIG_SYS_SUPPORTS_ZBOOT_UART_PROM) += $(obj)/uart-prom.o
vmlinuzobjs-$(CONFIG_MIPS_ALCHEMY)		   += $(obj)/uart-alchemy.o
vmlinuzobjs-$(CONFIG_ATH79)			   += $(obj)/uart-ath79.o
endif

vmlinuzobjs-$(CONFIG_KERNEL_XZ) += $(obj)/ashldi3.o

vmlinuzobjs-$(CONFIG_KERNEL_ZSTD) += $(obj)/bswapdi.o $(obj)/ashldi3.o $(obj)/clz_ctz.o

targets := $(notdir $(vmlinuzobjs-y))

targets += vmlinux.bin

OBJCOPYFLAGS_vmlinux.bin := $(OBJCOPYFLAGS) -O binary -R .comment -S

$(obj)/vmlinux.bin: $(KBUILD_IMAGE) FORCE
	$(call if_changed,objcopy)

tool_$(CONFIG_KERNEL_GZIP)    = gzip
tool_$(CONFIG_KERNEL_BZIP2)   = bzip2_with_size
tool_$(CONFIG_KERNEL_LZ4)     = lz4_with_size
tool_$(CONFIG_KERNEL_LZMA)    = lzma_with_size
tool_$(CONFIG_KERNEL_LZO)     = lzo_with_size
tool_$(CONFIG_KERNEL_XZ)      = xzkern_with_size
tool_$(CONFIG_KERNEL_ZSTD)    = zstd22_with_size

targets += vmlinux.bin.z

$(obj)/vmlinux.bin.z: $(obj)/vmlinux.bin FORCE
	$(call if_changed,$(tool_y))

targets += piggy.o dummy.o

OBJCOPYFLAGS_piggy.o := --add-section=.image=$(obj)/vmlinux.bin.z \
			--set-section-flags=.image=contents,alloc,load,readonly,data

$(obj)/piggy.o: $(obj)/dummy.o $(obj)/vmlinux.bin.z FORCE
	$(call if_changed,objcopy)

HOSTCFLAGS_calc_vmlinuz_load_addr.o += $(LINUXINCLUDE)

# Calculate the load address of the compressed kernel image
hostprogs := calc_vmlinuz_load_addr

ifneq (0x0,$(CONFIG_ZBOOT_LOAD_ADDRESS))
zload-y	= $(CONFIG_ZBOOT_LOAD_ADDRESS)
endif

ifneq ($(zload-y),)
VMLINUZ_LOAD_ADDRESS := $(zload-y)
else
VMLINUZ_LOAD_ADDRESS = $(shell $(obj)/calc_vmlinuz_load_addr \
		$(obj)/vmlinux.bin $(LINKER_LOAD_ADDRESS))
endif
UIMAGE_LOADADDR = $(VMLINUZ_LOAD_ADDRESS)

vmlinuzobjs-y += $(obj)/piggy.o

targets += ../../../../vmlinuz

quiet_cmd_zld = LD      $@
      cmd_zld = $(LD) $(KBUILD_LDFLAGS) -Ttext $(VMLINUZ_LOAD_ADDRESS) -T $< $(vmlinuzobjs-y) -o $@
quiet_cmd_strip = STRIP   $@
      cmd_strip = $(STRIP) -s $@

$(objtree)/vmlinuz: $(src)/ld.script $(vmlinuzobjs-y) $(obj)/calc_vmlinuz_load_addr
	$(call cmd,zld)
	$(call cmd,strip)

objboot := $(objtree)/arch/mips/boot

$(objboot)/vmlinuz: $(objtree)/vmlinuz FORCE

#
# Some DECstations need all possible sections of an ECOFF executable
#
ifdef CONFIG_MACH_DECSTATION
  e2eflag := -a
endif

# elf2ecoff can only handle 32bit image
hostprogs += ../elf2ecoff

ifdef CONFIG_32BIT
	VMLINUZ = $(objtree)/vmlinuz
else
	VMLINUZ = $(objboot)/vmlinuz.32
endif

targets += ../vmlinuz.32

quiet_cmd_32 = OBJCOPY $@
      cmd_32 = $(OBJCOPY) -O $(32bit-bfd) $(OBJCOPYFLAGS) $< $@

$(objboot)/vmlinuz.32: $(objtree)/vmlinuz
	$(call cmd,32)

targets += ../vmlinuz.ecoff

quiet_cmd_ecoff = ECOFF	  $@
      cmd_ecoff = $< $(VMLINUZ) $@ $(e2eflag)

$(objboot)/vmlinuz.ecoff: $(objboot)/elf2ecoff $(VMLINUZ)
	$(call cmd,ecoff)

targets += ../vmlinuz.bin

OBJCOPYFLAGS_vmlinuz.bin := $(OBJCOPYFLAGS) -O binary

$(objboot)/vmlinuz.bin: $(objtree)/vmlinuz
	$(call cmd,objcopy)

targets += ../vmlinuz.srec

OBJCOPYFLAGS_vmlinuz.srec := $(OBJCOPYFLAGS) -S -O srec

$(objboot)/vmlinuz.srec: $(objtree)/vmlinuz
	$(call cmd,objcopy)

targets += ../uzImage.bin

$(objboot)/uzImage.bin: $(objboot)/vmlinuz.bin FORCE
	$(call if_changed,uimage,none)

#
# Flattened Image Tree (.itb) image
#

ifeq ($(ADDR_BITS),32)
itb_addr_cells = 1
endif
ifeq ($(ADDR_BITS),64)
itb_addr_cells = 2
endif

targets += ../vmlinuz.its.S

quiet_cmd_its_cat = CAT     $@
      cmd_its_cat = cat $(real-prereqs) >$@

$(objboot)/vmlinuz.its.S: $(addprefix $(srctree)/arch/mips/$(PLATFORM)/,$(ITS_INPUTS)) FORCE
	$(call if_changed,its_cat)

targets += ../vmlinuz.its

quiet_cmd_cpp_its_S = ITS     $@
      cmd_cpp_its_S = $(CPP) -P -C -o $@ $< \
		        -DKERNEL_NAME="\"Linux $(KERNELRELEASE)\"" \
			-DVMLINUX_BINARY="\"$(2)\"" \
			-DVMLINUX_COMPRESSION="\"none\"" \
			-DVMLINUX_LOAD_ADDRESS=$(VMLINUZ_LOAD_ADDRESS) \
			-DVMLINUX_ENTRY_ADDRESS=$(VMLINUZ_LOAD_ADDRESS) \
			-DADDR_BITS=$(ADDR_BITS) \
			-DADDR_CELLS=$(itb_addr_cells)

$(objboot)/vmlinuz.its: $(objboot)/vmlinuz.its.S FORCE
	$(call if_changed,cpp_its_S,vmlinuz.bin)

targets += ../vmlinuz.itb

quiet_cmd_itb-image = ITB     $@
      cmd_itb-image = \
		env PATH="$(objtree)/scripts/dtc:$(PATH)" \
		$(BASH) $(MKIMAGE) \
		-D "-I dts -O dtb -p 500 \
			--include $(objtree)/arch/mips \
			--warning no-unit_address_vs_reg" \
		-f $(2) $@

$(objboot)/vmlinuz.itb: $(objboot)/vmlinuz.its $(objboot)/vmlinuz.bin FORCE
	$(call if_changed,itb-image,$<)
