#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995, 1998, 2001, 2002 by Ralf Baechle
# Copyright (C) 2004  Maciej W. Rozycki
#

#
# Some DECstations need all possible sections of an ECOFF executable
#
ifdef CONFIG_MACH_DECSTATION
  e2eflag := -a
endif

#
# Drop some uninteresting sections in the kernel.
# This is only relevant for ELF kernels but doesn't hurt a.out
#
drop-sections := .reginfo .mdebug .comment .note .pdr .options .MIPS.options
strip-flags   := $(addprefix --remove-section=,$(drop-sections))

hostprogs-y := elf2ecoff

suffix-y			:= bin
suffix-$(CONFIG_KERNEL_BZIP2)	:= bz2
suffix-$(CONFIG_KERNEL_GZIP)	:= gz
suffix-$(CONFIG_KERNEL_LZMA)	:= lzma
suffix-$(CONFIG_KERNEL_LZO)	:= lzo

targets := vmlinux.ecoff
quiet_cmd_ecoff = ECOFF	  $@
      cmd_ecoff = $(obj)/elf2ecoff $(VMLINUX) $@ $(e2eflag)
$(obj)/vmlinux.ecoff: $(obj)/elf2ecoff $(VMLINUX) FORCE
	$(call if_changed,ecoff)

targets += vmlinux.bin
quiet_cmd_bin = OBJCOPY $@
      cmd_bin = $(OBJCOPY) -O binary $(strip-flags) $(VMLINUX) $@
$(obj)/vmlinux.bin: $(VMLINUX) FORCE
	$(call if_changed,bin)

targets += vmlinux.srec
quiet_cmd_srec = OBJCOPY $@
      cmd_srec = $(OBJCOPY) -S -O srec $(strip-flags) $(VMLINUX) $@
$(obj)/vmlinux.srec: $(VMLINUX) FORCE
	$(call if_changed,srec)

UIMAGE_LOADADDR  = $(VMLINUX_LOAD_ADDRESS)
UIMAGE_ENTRYADDR = $(VMLINUX_ENTRY_ADDRESS)

#
# Compressed vmlinux images
#

extra-y += vmlinux.bin.bz2
extra-y += vmlinux.bin.gz
extra-y += vmlinux.bin.lzma
extra-y += vmlinux.bin.lzo

$(obj)/vmlinux.bin.bz2: $(obj)/vmlinux.bin FORCE
	$(call if_changed,bzip2)

$(obj)/vmlinux.bin.gz: $(obj)/vmlinux.bin FORCE
	$(call if_changed,gzip)

$(obj)/vmlinux.bin.lzma: $(obj)/vmlinux.bin FORCE
	$(call if_changed,lzma)

$(obj)/vmlinux.bin.lzo: $(obj)/vmlinux.bin FORCE
	$(call if_changed,lzo)

#
# Compressed u-boot images
#

targets += uImage
targets += uImage.bin
targets += uImage.bz2
targets += uImage.gz
targets += uImage.lzma
targets += uImage.lzo

$(obj)/uImage.bin: $(obj)/vmlinux.bin FORCE
	$(call if_changed,uimage,none)

$(obj)/uImage.bz2: $(obj)/vmlinux.bin.bz2 FORCE
	$(call if_changed,uimage,bzip2)

$(obj)/uImage.gz: $(obj)/vmlinux.bin.gz FORCE
	$(call if_changed,uimage,gzip)

$(obj)/uImage.lzma: $(obj)/vmlinux.bin.lzma FORCE
	$(call if_changed,uimage,lzma)

$(obj)/uImage.lzo: $(obj)/vmlinux.bin.lzo FORCE
	$(call if_changed,uimage,lzo)

$(obj)/uImage: $(obj)/uImage.$(suffix-y)
	@ln -sf $(notdir $<) $@
	@echo '  Image $@ is ready'

#
# Flattened Image Tree (.itb) images
#

targets += vmlinux.itb
targets += vmlinux.gz.itb
targets += vmlinux.bz2.itb
targets += vmlinux.lzma.itb
targets += vmlinux.lzo.itb

ifeq ($(ADDR_BITS),32)
	itb_addr_cells = 1
endif
ifeq ($(ADDR_BITS),64)
	itb_addr_cells = 2
endif

quiet_cmd_its_cat = CAT     $@
      cmd_its_cat = cat $^ >$@

$(obj)/vmlinux.its.S: $(addprefix $(srctree)/arch/mips/$(PLATFORM)/,$(ITS_INPUTS))
	$(call if_changed,its_cat)

quiet_cmd_cpp_its_S = ITS     $@
      cmd_cpp_its_S = $(CPP) $(cpp_flags) -P -C -o $@ $< \
		        -DKERNEL_NAME="\"Linux $(KERNELRELEASE)\"" \
			-DVMLINUX_BINARY="\"$(3)\"" \
			-DVMLINUX_COMPRESSION="\"$(2)\"" \
			-DVMLINUX_LOAD_ADDRESS=$(VMLINUX_LOAD_ADDRESS) \
			-DVMLINUX_ENTRY_ADDRESS=$(VMLINUX_ENTRY_ADDRESS) \
			-DADDR_BITS=$(ADDR_BITS) \
			-DADDR_CELLS=$(itb_addr_cells)

$(obj)/vmlinux.its: $(obj)/vmlinux.its.S $(VMLINUX) FORCE
	$(call if_changed_dep,cpp_its_S,none,vmlinux.bin)

$(obj)/vmlinux.gz.its: $(obj)/vmlinux.its.S $(VMLINUX) FORCE
	$(call if_changed_dep,cpp_its_S,gzip,vmlinux.bin.gz)

$(obj)/vmlinux.bz2.its: $(obj)/vmlinux.its.S $(VMLINUX)  FORCE
	$(call if_changed_dep,cpp_its_S,bzip2,vmlinux.bin.bz2)

$(obj)/vmlinux.lzma.its: $(obj)/vmlinux.its.S $(VMLINUX) FORCE
	$(call if_changed_dep,cpp_its_S,lzma,vmlinux.bin.lzma)

$(obj)/vmlinux.lzo.its: $(obj)/vmlinux.its.S $(VMLINUX) FORCE
	$(call if_changed_dep,cpp_its_S,lzo,vmlinux.bin.lzo)

quiet_cmd_itb-image = ITB     $@
      cmd_itb-image = \
		env PATH="$(objtree)/scripts/dtc:$(PATH)" \
		$(CONFIG_SHELL) $(MKIMAGE) \
		-D "-I dts -O dtb -p 500 \
			--include $(objtree)/arch/mips \
			--warning no-unit_address_vs_reg" \
		-f $(2) $@

$(obj)/vmlinux.itb: $(obj)/vmlinux.its $(obj)/vmlinux.bin FORCE
	$(call if_changed,itb-image,$<)

$(obj)/vmlinux.gz.itb: $(obj)/vmlinux.gz.its $(obj)/vmlinux.bin.gz FORCE
	$(call if_changed,itb-image,$<)

$(obj)/vmlinux.bz2.itb: $(obj)/vmlinux.bz2.its $(obj)/vmlinux.bin.bz2 FORCE
	$(call if_changed,itb-image,$<)

$(obj)/vmlinux.lzma.itb: $(obj)/vmlinux.lzma.its $(obj)/vmlinux.bin.lzma FORCE
	$(call if_changed,itb-image,$<)

$(obj)/vmlinux.lzo.itb: $(obj)/vmlinux.lzo.its $(obj)/vmlinux.bin.lzo FORCE
	$(call if_changed,itb-image,$<)
