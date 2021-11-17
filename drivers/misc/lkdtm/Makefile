# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_LKDTM)		+= lkdtm.o

lkdtm-$(CONFIG_LKDTM)		+= core.o
lkdtm-$(CONFIG_LKDTM)		+= bugs.o
lkdtm-$(CONFIG_LKDTM)		+= heap.o
lkdtm-$(CONFIG_LKDTM)		+= perms.o
lkdtm-$(CONFIG_LKDTM)		+= refcount.o
lkdtm-$(CONFIG_LKDTM)		+= rodata_objcopy.o
lkdtm-$(CONFIG_LKDTM)		+= usercopy.o
lkdtm-$(CONFIG_LKDTM)		+= stackleak.o
lkdtm-$(CONFIG_LKDTM)		+= cfi.o
lkdtm-$(CONFIG_LKDTM)		+= fortify.o
lkdtm-$(CONFIG_PPC_BOOK3S_64)	+= powerpc.o

KASAN_SANITIZE_rodata.o		:= n
KASAN_SANITIZE_stackleak.o	:= n
KCOV_INSTRUMENT_rodata.o	:= n
CFLAGS_REMOVE_rodata.o		+= $(CC_FLAGS_LTO)

OBJCOPYFLAGS :=
OBJCOPYFLAGS_rodata_objcopy.o	:= \
			--rename-section .noinstr.text=.rodata,alloc,readonly,load
targets += rodata.o rodata_objcopy.o
$(obj)/rodata_objcopy.o: $(obj)/rodata.o FORCE
	$(call if_changed,objcopy)
