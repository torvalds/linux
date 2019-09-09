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

KASAN_SANITIZE_stackleak.o	:= n
KCOV_INSTRUMENT_rodata.o	:= n

OBJCOPYFLAGS :=
OBJCOPYFLAGS_rodata_objcopy.o	:= \
			--rename-section .text=.rodata,alloc,readonly,load
targets += rodata.o rodata_objcopy.o
$(obj)/rodata_objcopy.o: $(obj)/rodata.o FORCE
	$(call if_changed,objcopy)
