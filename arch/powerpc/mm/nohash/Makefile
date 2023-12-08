# SPDX-License-Identifier: GPL-2.0

ccflags-$(CONFIG_PPC64)	:= $(NO_MINIMAL_TOC)

obj-y				+= mmu_context.o tlb.o tlb_low.o kup.o
obj-$(CONFIG_PPC_BOOK3E_64)  	+= tlb_low_64e.o book3e_pgtable.o
obj-$(CONFIG_40x)		+= 40x.o
obj-$(CONFIG_44x)		+= 44x.o
obj-$(CONFIG_PPC_8xx)		+= 8xx.o
obj-$(CONFIG_PPC_E500)		+= e500.o
obj-$(CONFIG_RANDOMIZE_BASE)	+= kaslr_booke.o
ifdef CONFIG_HUGETLB_PAGE
obj-$(CONFIG_PPC_E500)	+= e500_hugetlbpage.o
endif

# Disable kcov instrumentation on sensitive code
# This is necessary for booting with kcov enabled on book3e machines
KCOV_INSTRUMENT_tlb.o := n
KCOV_INSTRUMENT_e500.o := n
