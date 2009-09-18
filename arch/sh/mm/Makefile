#
# Makefile for the Linux SuperH-specific parts of the memory manager.
#

obj-y			:= cache.o init.o consistent.o mmap.o

cacheops-$(CONFIG_CPU_SH2)		:= cache-sh2.o
cacheops-$(CONFIG_CPU_SH2A)		:= cache-sh2a.o
cacheops-$(CONFIG_CPU_SH3)		:= cache-sh3.o
cacheops-$(CONFIG_CPU_SH4)		:= cache-sh4.o flush-sh4.o
cacheops-$(CONFIG_CPU_SH5)		:= cache-sh5.o flush-sh4.o
cacheops-$(CONFIG_SH7705_CACHE_32KB)	+= cache-sh7705.o

obj-y			+= $(cacheops-y)

mmu-y			:= nommu.o extable_32.o
mmu-$(CONFIG_MMU)	:= extable_$(BITS).o fault_$(BITS).o \
			   ioremap_$(BITS).o kmap.o tlbflush_$(BITS).o

obj-y			+= $(mmu-y)
obj-$(CONFIG_DEBUG_FS)	+= asids-debugfs.o

ifdef CONFIG_DEBUG_FS
obj-$(CONFIG_CPU_SH4)	+= cache-debugfs.o
endif

ifdef CONFIG_MMU
tlb-$(CONFIG_CPU_SH3)		:= tlb-sh3.o
tlb-$(CONFIG_CPU_SH4)		:= tlb-sh4.o
tlb-$(CONFIG_CPU_SH5)		:= tlb-sh5.o
tlb-$(CONFIG_CPU_HAS_PTEAEX)	:= tlb-pteaex.o
obj-y				+= $(tlb-y)
endif

obj-$(CONFIG_HUGETLB_PAGE)	+= hugetlbpage.o
obj-$(CONFIG_PMB)		+= pmb.o
obj-$(CONFIG_PMB_FIXED)		+= pmb-fixed.o
obj-$(CONFIG_NUMA)		+= numa.o

# Special flags for fault_64.o.  This puts restrictions on the number of
# caller-save registers that the compiler can target when building this file.
# This is required because the code is called from a context in entry.S where
# very few registers have been saved in the exception handler (for speed
# reasons).
# The caller save registers that have been saved and which can be used are
# r2,r3,r4,r5 : argument passing
# r15, r18 : SP and LINK
# tr0-4 : allow all caller-save TR's.  The compiler seems to be able to make
#         use of them, so it's probably beneficial to performance to save them
#         and have them available for it.
#
# The resources not listed below are callee save, i.e. the compiler is free to
# use any of them and will spill them to the stack itself.

CFLAGS_fault_64.o += -ffixed-r7 \
	-ffixed-r8 -ffixed-r9 -ffixed-r10 -ffixed-r11 -ffixed-r12 \
	-ffixed-r13 -ffixed-r14 -ffixed-r16 -ffixed-r17 -ffixed-r19 \
	-ffixed-r20 -ffixed-r21 -ffixed-r22 -ffixed-r23 \
	-ffixed-r24 -ffixed-r25 -ffixed-r26 -ffixed-r27 \
	-ffixed-r36 -ffixed-r37 -ffixed-r38 -ffixed-r39 -ffixed-r40 \
	-ffixed-r41 -ffixed-r42 -ffixed-r43  \
	-ffixed-r60 -ffixed-r61 -ffixed-r62 \
	-fomit-frame-pointer

EXTRA_CFLAGS += -Werror
