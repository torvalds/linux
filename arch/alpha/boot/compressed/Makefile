# SPDX-License-Identifier: GPL-2.0-only
#
# Create a self-extracting compressed vmlinux image from the original
# vmlinux. aboot loads and enters it like a plain vmlinux.
#

OBJECTS := head.o misc.o trampoline.o piggy.o
OBJECTS += memcpy.o memmove.o memset.o
OBJECTS += __divqu.o __remqu.o __divlu.o __remlu.o
targets := vmlinux.lds vmlinux vmlinux.bin
targets += vmlinux.bin.bz2 vmlinux.bin.gz vmlinux.bin.lz4
targets += vmlinux.bin.lzma vmlinux.bin.lzo vmlinux.bin.xz
targets += vmlinux.bin.zst
targets += $(OBJECTS)

# The stub runs before the kernel has identified the CPU it booted on, so
# build it for the base Alpha instruction set rather than inheriting the
# kernel's -mcpu=.
KBUILD_CFLAGS := -D__KERNEL__ -std=gnu11 -Os -fno-strict-aliasing
KBUILD_CFLAGS += -fno-PIE -ffreestanding -fno-stack-protector
KBUILD_CFLAGS += -fno-asynchronous-unwind-tables
KBUILD_CFLAGS += -pipe -mcpu=ev4 -mno-fp-regs -ffixed-8
KBUILD_CFLAGS += -DDISABLE_BRANCH_PROFILING -D__DISABLE_EXPORTS
KBUILD_CFLAGS += $(CC_FLAGS_DIALECT)
KBUILD_AFLAGS := -D__ASSEMBLY__ $(KBUILD_CFLAGS)

# The stub has no sanitizer or coverage runtime. arch/alpha/lib/lib.a is
# built with the kernel's instrumentation and -mcpu=, so build private
# copies of the library routines the stub needs instead of linking it.
UBSAN_SANITIZE := n
GCOV_PROFILE := n

$(obj)/memcpy.o: $(srctree)/arch/alpha/lib/memcpy.c FORCE
	$(call if_changed_dep,cc_o_c)

$(obj)/memmove.o: $(srctree)/arch/alpha/lib/memmove.S FORCE
	$(call if_changed_dep,as_o_S)

$(obj)/memset.o: $(srctree)/arch/alpha/lib/memset.S FORCE
	$(call if_changed_dep,as_o_S)

AFLAGS___divqu.o := -DDIV
AFLAGS___remqu.o := -DREM
AFLAGS___divlu.o := -DDIV -DINTSIZE
AFLAGS___remlu.o := -DREM -DINTSIZE

$(addprefix $(obj)/,__divqu.o __remqu.o __divlu.o __remlu.o): $(srctree)/arch/alpha/lib/divide.S FORCE
	$(call if_changed_dep,as_o_S)

# zstd's decompressor uses __builtin_ctzll()/__builtin_clzll(). Those are
# inline code only with the CIX extension, which the base instruction set
# the stub is built for does not have, so they become calls to libgcc's
# __ctzdi2/__clzdi2.
LIBGCC := $(shell $(CC) -print-libgcc-file-name)

LDFLAGS_vmlinux := -X --orphan-handling=warn -T
$(obj)/vmlinux: $(obj)/vmlinux.lds $(addprefix $(obj)/, $(OBJECTS)) $(LIBGCC) FORCE
	$(call if_changed,ld)

OBJCOPYFLAGS_vmlinux.bin := -R .comment -R .note -S
$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

suffix-$(CONFIG_KERNEL_BZIP2)	:= bz2
suffix-$(CONFIG_KERNEL_GZIP)	:= gz
suffix-$(CONFIG_KERNEL_LZ4)	:= lz4
suffix-$(CONFIG_KERNEL_LZMA)	:= lzma
suffix-$(CONFIG_KERNEL_LZO)	:= lzo
suffix-$(CONFIG_KERNEL_XZ)	:= xz
suffix-$(CONFIG_KERNEL_ZSTD)	:= zst

$(obj)/vmlinux.bin.bz2: $(obj)/vmlinux.bin FORCE
	$(call if_changed,bzip2_with_size)
$(obj)/vmlinux.bin.gz: $(obj)/vmlinux.bin FORCE
	+$(call if_changed,gzip)
$(obj)/vmlinux.bin.lz4: $(obj)/vmlinux.bin FORCE
	$(call if_changed,lz4_with_size)
$(obj)/vmlinux.bin.lzma: $(obj)/vmlinux.bin FORCE
	$(call if_changed,lzma_with_size)
$(obj)/vmlinux.bin.lzo: $(obj)/vmlinux.bin FORCE
	$(call if_changed,lzo_with_size)
$(obj)/vmlinux.bin.xz: $(obj)/vmlinux.bin FORCE
	$(call if_changed,xzkern_with_size)
$(obj)/vmlinux.bin.zst: $(obj)/vmlinux.bin FORCE
	$(call if_changed,zstd22_with_size)

LDFLAGS_piggy.o := -r --format binary --oformat elf64-alpha -T
$(obj)/piggy.o: $(obj)/vmlinux.scr $(obj)/vmlinux.bin.$(suffix-y) FORCE
	$(call if_changed,ld)
