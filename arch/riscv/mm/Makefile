# SPDX-License-Identifier: GPL-2.0-only

CFLAGS_init.o := -mcmodel=medany
ifdef CONFIG_FTRACE
CFLAGS_REMOVE_init.o = -pg
endif

obj-y += init.o
obj-y += extable.o
obj-$(CONFIG_MMU) += fault.o pageattr.o
obj-y += cacheflush.o
obj-y += context.o

ifeq ($(CONFIG_MMU),y)
obj-$(CONFIG_SMP) += tlbflush.o
endif
obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o
obj-$(CONFIG_PTDUMP_CORE) += ptdump.o
obj-$(CONFIG_KASAN)   += kasan_init.o

ifdef CONFIG_KASAN
KASAN_SANITIZE_kasan_init.o := n
KASAN_SANITIZE_init.o := n
endif

obj-$(CONFIG_DEBUG_VIRTUAL) += physaddr.o
