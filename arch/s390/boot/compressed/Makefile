# SPDX-License-Identifier: GPL-2.0
#
# linux/arch/s390/boot/compressed/Makefile
#
# create a compressed vmlinux image from the original vmlinux
#

KCOV_INSTRUMENT := n
GCOV_PROFILE := n
UBSAN_SANITIZE := n
KASAN_SANITIZE := n

obj-y	:= $(if $(CONFIG_KERNEL_UNCOMPRESSED),,decompressor.o) piggy.o info.o
targets	:= vmlinux.lds vmlinux vmlinux.bin vmlinux.bin.gz vmlinux.bin.bz2
targets += vmlinux.bin.xz vmlinux.bin.lzma vmlinux.bin.lzo vmlinux.bin.lz4
targets += info.bin $(obj-y)

KBUILD_AFLAGS := $(KBUILD_AFLAGS_DECOMPRESSOR)
KBUILD_CFLAGS := $(KBUILD_CFLAGS_DECOMPRESSOR)
OBJCOPYFLAGS :=

OBJECTS := $(addprefix $(obj)/,$(obj-y))

LDFLAGS_vmlinux := --oformat $(LD_BFD) -e startup -T
$(obj)/vmlinux: $(obj)/vmlinux.lds $(objtree)/arch/s390/boot/startup.a $(OBJECTS) FORCE
	$(call if_changed,ld)

OBJCOPYFLAGS_info.bin := -O binary --only-section=.vmlinux.info --set-section-flags .vmlinux.info=load
$(obj)/info.bin: vmlinux FORCE
	$(call if_changed,objcopy)

OBJCOPYFLAGS_info.o := -I binary -O elf64-s390 -B s390:64-bit --rename-section .data=.vmlinux.info
$(obj)/info.o: $(obj)/info.bin FORCE
	$(call if_changed,objcopy)

OBJCOPYFLAGS_vmlinux.bin := -O binary --remove-section=.comment --remove-section=.vmlinux.info -S
$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

vmlinux.bin.all-y := $(obj)/vmlinux.bin

suffix-$(CONFIG_KERNEL_GZIP)  := .gz
suffix-$(CONFIG_KERNEL_BZIP2) := .bz2
suffix-$(CONFIG_KERNEL_LZ4)  := .lz4
suffix-$(CONFIG_KERNEL_LZMA)  := .lzma
suffix-$(CONFIG_KERNEL_LZO)  := .lzo
suffix-$(CONFIG_KERNEL_XZ)  := .xz

$(obj)/vmlinux.bin.gz: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,gzip)
$(obj)/vmlinux.bin.bz2: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,bzip2)
$(obj)/vmlinux.bin.lz4: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,lz4)
$(obj)/vmlinux.bin.lzma: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,lzma)
$(obj)/vmlinux.bin.lzo: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,lzo)
$(obj)/vmlinux.bin.xz: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,xzkern)

OBJCOPYFLAGS_piggy.o := -I binary -O elf64-s390 -B s390:64-bit --rename-section .data=.vmlinux.bin.compressed
$(obj)/piggy.o: $(obj)/vmlinux.bin$(suffix-y) FORCE
	$(call if_changed,objcopy)
