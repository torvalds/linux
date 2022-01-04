# SPDX-License-Identifier: GPL-2.0

KASAN_SANITIZE_mmu.o := n

ifdef CONFIG_KASAN
CFLAGS_mmu.o  		+= -DDISABLE_BRANCH_PROFILING
endif

obj-y += mmu.o mmu_context.o
obj-$(CONFIG_PPC_BOOK3S_603) += nohash_low.o
obj-$(CONFIG_PPC_BOOK3S_604) += hash_low.o tlb.o
obj-$(CONFIG_PPC_KUEP) += kuep.o
obj-$(CONFIG_PPC_KUAP) += kuap.o
