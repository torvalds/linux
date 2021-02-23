# SPDX-License-Identifier: GPL-2.0

KASAN_SANITIZE_mmu.o := n

ifdef CONFIG_KASAN
CFLAGS_mmu.o  		+= -DDISABLE_BRANCH_PROFILING
endif

obj-y += mmu.o hash_low.o mmu_context.o tlb.o nohash_low.o
