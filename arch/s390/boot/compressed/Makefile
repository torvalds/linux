#
# linux/arch/s390/boot/compressed/Makefile
#
# create a compressed vmlinux image from the original vmlinux
#

KCOV_INSTRUMENT := n

targets	:= vmlinux.lds vmlinux vmlinux.bin vmlinux.bin.gz vmlinux.bin.bz2
targets += vmlinux.bin.xz vmlinux.bin.lzma vmlinux.bin.lzo vmlinux.bin.lz4
targets += misc.o piggy.o sizes.h head.o

KBUILD_CFLAGS := -m64 -D__KERNEL__ -O2
KBUILD_CFLAGS += -DDISABLE_BRANCH_PROFILING
KBUILD_CFLAGS += $(cflags-y) -fno-delete-null-pointer-checks -msoft-float
KBUILD_CFLAGS += $(call cc-option,-mpacked-stack)
KBUILD_CFLAGS += $(call cc-option,-ffreestanding)

GCOV_PROFILE := n
UBSAN_SANITIZE := n

OBJECTS := $(addprefix $(objtree)/arch/s390/kernel/, head.o sclp.o ebcdic.o als.o)
OBJECTS += $(obj)/head.o $(obj)/misc.o $(obj)/piggy.o

LDFLAGS_vmlinux := --oformat $(LD_BFD) -e startup -T
$(obj)/vmlinux: $(obj)/vmlinux.lds $(OBJECTS)
	$(call if_changed,ld)

sed-sizes := -e 's/^\([0-9a-fA-F]*\) . \(__bss_start\|_end\)$$/\#define SZ\2 0x\1/p'

quiet_cmd_sizes = GEN $@
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

suffix-$(CONFIG_KERNEL_GZIP)  := gz
suffix-$(CONFIG_KERNEL_BZIP2) := bz2
suffix-$(CONFIG_KERNEL_LZ4)  := lz4
suffix-$(CONFIG_KERNEL_LZMA)  := lzma
suffix-$(CONFIG_KERNEL_LZO)  := lzo
suffix-$(CONFIG_KERNEL_XZ)  := xz

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
$(obj)/piggy.o: $(obj)/vmlinux.scr $(obj)/vmlinux.bin.$(suffix-y)
	$(call if_changed,ld)
