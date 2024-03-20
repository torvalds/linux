# SPDX-License-Identifier: GPL-2.0-only

CFLAGS_init.o := -mcmodel=medany
ifdef CONFIG_RELOCATABLE
CFLAGS_init.o += -fno-pie
endif

ifdef CONFIG_FTRACE
CFLAGS_REMOVE_init.o = $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_cacheflush.o = $(CC_FLAGS_FTRACE)
endif

KCOV_INSTRUMENT_init.o := n

obj-y += init.o
obj-$(CONFIG_MMU) += extable.o fault.o pageattr.o
obj-y += cacheflush.o
obj-y += context.o
obj-y += pgtable.o
obj-y += pmem.o

ifeq ($(CONFIG_MMU),y)
obj-$(CONFIG_SMP) += tlbflush.o
endif
obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o
obj-$(CONFIG_PTDUMP_CORE) += ptdump.o
obj-$(CONFIG_KASAN)   += kasan_init.o

ifdef CONFIG_KASAN
KASAN_SANITIZE_kasan_init.o := n
KASAN_SANITIZE_init.o := n
ifdef CONFIG_DEBUG_VIRTUAL
KASAN_SANITIZE_physaddr.o := n
endif
endif

obj-$(CONFIG_DEBUG_VIRTUAL) += physaddr.o
obj-$(CONFIG_RISCV_DMA_NONCOHERENT) += dma-noncoherent.o
obj-$(CONFIG_RISCV_NONSTANDARD_CACHE_OPS) += cache-ops.o
