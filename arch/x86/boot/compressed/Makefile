#
# linux/arch/x86/boot/compressed/Makefile
#
# create a compressed vmlinux image from the original vmlinux
#
# vmlinuz is:
#	decompression code (*.o)
#	asm globals (piggy.S), including:
#		vmlinux.bin.(gz|bz2|lzma|...)
#
# vmlinux.bin is:
#	vmlinux stripped of debugging and comments
# vmlinux.bin.all is:
#	vmlinux.bin + vmlinux.relocs
# vmlinux.bin.(gz|bz2|lzma|...) is:
#	(see scripts/Makefile.lib size_append)
#	compressed vmlinux.bin.all + u32 size of vmlinux.bin.all

KASAN_SANITIZE := n

# Prevents link failures: __sanitizer_cov_trace_pc() is not linked in.
KCOV_INSTRUMENT		:= n

targets := vmlinux vmlinux.bin vmlinux.bin.gz vmlinux.bin.bz2 vmlinux.bin.lzma \
	vmlinux.bin.xz vmlinux.bin.lzo vmlinux.bin.lz4

KBUILD_CFLAGS := -m$(BITS) -D__KERNEL__ $(LINUX_INCLUDE) -O2
KBUILD_CFLAGS += -fno-strict-aliasing $(call cc-option, -fPIE, -fPIC)
KBUILD_CFLAGS += -DDISABLE_BRANCH_PROFILING
cflags-$(CONFIG_X86_32) := -march=i386
cflags-$(CONFIG_X86_64) := -mcmodel=small
KBUILD_CFLAGS += $(cflags-y)
KBUILD_CFLAGS += -mno-mmx -mno-sse
KBUILD_CFLAGS += $(call cc-option,-ffreestanding)
KBUILD_CFLAGS += $(call cc-option,-fno-stack-protector)

KBUILD_AFLAGS  := $(KBUILD_CFLAGS) -D__ASSEMBLY__
GCOV_PROFILE := n

LDFLAGS := -m elf_$(UTS_MACHINE)
ifeq ($(CONFIG_RELOCATABLE),y)
# If kernel is relocatable, build compressed kernel as PIE.
ifeq ($(CONFIG_X86_32),y)
LDFLAGS += $(call ld-option, -pie) $(call ld-option, --no-dynamic-linker)
else
# To build 64-bit compressed kernel as PIE, we disable relocation
# overflow check to avoid relocation overflow error with a new linker
# command-line option, -z noreloc-overflow.
LDFLAGS += $(shell $(LD) --help 2>&1 | grep -q "\-z noreloc-overflow" \
	&& echo "-z noreloc-overflow -pie --no-dynamic-linker")
endif
endif
LDFLAGS_vmlinux := -T

hostprogs-y	:= mkpiggy
HOST_EXTRACFLAGS += -I$(srctree)/tools/include

vmlinux-objs-y := $(obj)/vmlinux.lds $(obj)/head_$(BITS).o $(obj)/misc.o \
	$(obj)/string.o $(obj)/cmdline.o \
	$(obj)/piggy.o $(obj)/cpuflags.o

vmlinux-objs-$(CONFIG_EARLY_PRINTK) += $(obj)/early_serial_console.o
vmlinux-objs-$(CONFIG_RANDOMIZE_BASE) += $(obj)/aslr.o

$(obj)/eboot.o: KBUILD_CFLAGS += -fshort-wchar -mno-red-zone

vmlinux-objs-$(CONFIG_EFI_STUB) += $(obj)/eboot.o $(obj)/efi_stub_$(BITS).o \
	$(objtree)/drivers/firmware/efi/libstub/lib.a
vmlinux-objs-$(CONFIG_EFI_MIXED) += $(obj)/efi_thunk_$(BITS).o

$(obj)/vmlinux: $(vmlinux-objs-y) FORCE
	$(call if_changed,ld)
	@:

OBJCOPYFLAGS_vmlinux.bin :=  -R .comment -S
$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

targets += $(patsubst $(obj)/%,%,$(vmlinux-objs-y)) vmlinux.bin.all vmlinux.relocs

CMD_RELOCS = arch/x86/tools/relocs
quiet_cmd_relocs = RELOCS  $@
      cmd_relocs = $(CMD_RELOCS) $< > $@;$(CMD_RELOCS) --abs-relocs $<
$(obj)/vmlinux.relocs: vmlinux FORCE
	$(call if_changed,relocs)

vmlinux.bin.all-y := $(obj)/vmlinux.bin
vmlinux.bin.all-$(CONFIG_X86_NEED_RELOCS) += $(obj)/vmlinux.relocs

$(obj)/vmlinux.bin.gz: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,gzip)
$(obj)/vmlinux.bin.bz2: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,bzip2)
$(obj)/vmlinux.bin.lzma: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,lzma)
$(obj)/vmlinux.bin.xz: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,xzkern)
$(obj)/vmlinux.bin.lzo: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,lzo)
$(obj)/vmlinux.bin.lz4: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,lz4)

suffix-$(CONFIG_KERNEL_GZIP)	:= gz
suffix-$(CONFIG_KERNEL_BZIP2)	:= bz2
suffix-$(CONFIG_KERNEL_LZMA)	:= lzma
suffix-$(CONFIG_KERNEL_XZ)	:= xz
suffix-$(CONFIG_KERNEL_LZO) 	:= lzo
suffix-$(CONFIG_KERNEL_LZ4) 	:= lz4

RUN_SIZE = $(shell $(OBJDUMP) -h vmlinux | \
	     $(CONFIG_SHELL) $(srctree)/arch/x86/tools/calc_run_size.sh)
quiet_cmd_mkpiggy = MKPIGGY $@
      cmd_mkpiggy = $(obj)/mkpiggy $< $(RUN_SIZE) > $@ || ( rm -f $@ ; false )

targets += piggy.S
$(obj)/piggy.S: $(obj)/vmlinux.bin.$(suffix-y) $(obj)/mkpiggy FORCE
	$(call if_changed,mkpiggy)
