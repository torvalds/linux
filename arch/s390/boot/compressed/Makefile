# SPDX-License-Identifier: GPL-2.0
#
# linux/arch/s390/boot/compressed/Makefile
#
# create a compressed vmlinux image from the original vmlinux
#

KCOV_INSTRUMENT := n
GCOV_PROFILE := n
UBSAN_SANITIZE := n

obj-y	:= $(if $(CONFIG_KERNEL_UNCOMPRESSED),,head.o misc.o) piggy.o
targets	:= vmlinux.lds vmlinux vmlinux.bin vmlinux.bin.gz vmlinux.bin.bz2
targets += vmlinux.bin.xz vmlinux.bin.lzma vmlinux.bin.lzo vmlinux.bin.lz4
targets += vmlinux.scr.lds $(obj-y) $(if $(CONFIG_KERNEL_UNCOMPRESSED),,sizes.h)

KBUILD_AFLAGS := $(KBUILD_AFLAGS_DECOMPRESSOR)
KBUILD_CFLAGS := $(KBUILD_CFLAGS_DECOMPRESSOR)

OBJECTS := $(addprefix $(obj)/,$(obj-y))

LDFLAGS_vmlinux := --oformat $(LD_BFD) -e startup -T
$(obj)/vmlinux: $(obj)/vmlinux.lds $(objtree)/arch/s390/boot/startup.a $(OBJECTS)
	$(call if_changed,ld)

# extract required uncompressed vmlinux symbols and adjust them to reflect offsets inside vmlinux.bin
sed-sizes := -e 's/^\([0-9a-fA-F]*\) . \(__bss_start\|_end\)$$/\#define SZ\2 (0x\1 - 0x100000)/p'

quiet_cmd_sizes = GEN     $@
      cmd_sizes = $(NM) $< | sed -n $(sed-sizes) > $@

$(obj)/sizes.h: vmlinux
	$(call if_changed,sizes)

AFLAGS_head.o += -I$(objtree)/$(obj)
$(obj)/head.o: $(obj)/sizes.h

CFLAGS_misc.o += -I$(objtree)/$(obj)
$(obj)/misc.o: $(obj)/sizes.h

OBJCOPYFLAGS_vmlinux.bin :=  -R .comment -S
$(obj)/vmlinux.bin: vmlinux
	$(call if_changed,objcopy)

vmlinux.bin.all-y := $(obj)/vmlinux.bin

suffix-$(CONFIG_KERNEL_GZIP)  := .gz
suffix-$(CONFIG_KERNEL_BZIP2) := .bz2
suffix-$(CONFIG_KERNEL_LZ4)  := .lz4
suffix-$(CONFIG_KERNEL_LZMA)  := .lzma
suffix-$(CONFIG_KERNEL_LZO)  := .lzo
suffix-$(CONFIG_KERNEL_XZ)  := .xz

$(obj)/vmlinux.bin.gz: $(vmlinux.bin.all-y)
	$(call if_changed,gzip)
$(obj)/vmlinux.bin.bz2: $(vmlinux.bin.all-y)
	$(call if_changed,bzip2)
$(obj)/vmlinux.bin.lz4: $(vmlinux.bin.all-y)
	$(call if_changed,lz4)
$(obj)/vmlinux.bin.lzma: $(vmlinux.bin.all-y)
	$(call if_changed,lzma)
$(obj)/vmlinux.bin.lzo: $(vmlinux.bin.all-y)
	$(call if_changed,lzo)
$(obj)/vmlinux.bin.xz: $(vmlinux.bin.all-y)
	$(call if_changed,xzkern)

LDFLAGS_piggy.o := -r --format binary --oformat $(LD_BFD) -T
$(obj)/piggy.o: $(obj)/vmlinux.scr.lds $(obj)/vmlinux.bin$(suffix-y)
	$(call if_changed,ld)

chkbss := $(filter-out $(obj)/misc.o $(obj)/piggy.o,$(OBJECTS))
chkbss-target := $(obj)/vmlinux.bin
include $(srctree)/arch/s390/scripts/Makefile.chkbss
