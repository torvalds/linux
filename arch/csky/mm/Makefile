# SPDX-License-Identifier: GPL-2.0-only
ifeq ($(CONFIG_CPU_HAS_CACHEV2),y)
obj-y +=			cachev2.o
CFLAGS_REMOVE_cachev2.o = $(CC_FLAGS_FTRACE)
else
obj-y +=			cachev1.o
CFLAGS_REMOVE_cachev1.o = $(CC_FLAGS_FTRACE)
endif

obj-y +=			dma-mapping.o
obj-y +=			fault.o
obj-$(CONFIG_HIGHMEM) +=	highmem.o
obj-y +=			init.o
obj-y +=			ioremap.o
obj-y +=			syscache.o
obj-y +=			tlb.o
obj-y +=			asid.o
obj-y +=			context.o
obj-$(CONFIG_HAVE_TCM) +=	tcm.o
