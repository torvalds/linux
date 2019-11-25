# SPDX-License-Identifier: GPL-2.0-only

CFLAGS_init.o := -mcmodel=medany
ifdef CONFIG_FTRACE
CFLAGS_REMOVE_init.o = -pg
endif

obj-y += init.o
obj-y += fault.o
obj-y += extable.o
obj-y += ioremap.o
obj-y += cacheflush.o
obj-y += context.o
obj-y += sifive_l2_cache.o

ifeq ($(CONFIG_MMU),y)
obj-$(CONFIG_SMP) += tlbflush.o
endif
obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o
