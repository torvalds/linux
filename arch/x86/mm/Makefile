# SPDX-License-Identifier: GPL-2.0
# Kernel does not boot with instrumentation of tlb.c and mem_encrypt*.c
KCOV_INSTRUMENT_tlb.o			:= n
KCOV_INSTRUMENT_mem_encrypt.o		:= n
KCOV_INSTRUMENT_mem_encrypt_identity.o	:= n

KASAN_SANITIZE_mem_encrypt.o		:= n
KASAN_SANITIZE_mem_encrypt_identity.o	:= n

ifdef CONFIG_FUNCTION_TRACER
CFLAGS_REMOVE_mem_encrypt.o		= -pg
CFLAGS_REMOVE_mem_encrypt_identity.o	= -pg
endif

obj-y	:=  init.o init_$(BITS).o fault.o ioremap.o extable.o pageattr.o mmap.o \
	    pat.o pgtable.o physaddr.o setup_nx.o tlb.o cpu_entry_area.o maccess.o

# Make sure __phys_addr has no stackprotector
nostackp := $(call cc-option, -fno-stack-protector)
CFLAGS_physaddr.o		:= $(nostackp)
CFLAGS_setup_nx.o		:= $(nostackp)
CFLAGS_mem_encrypt_identity.o	:= $(nostackp)

CFLAGS_fault.o := -I $(srctree)/$(src)/../include/asm/trace

obj-$(CONFIG_X86_PAT)		+= pat_interval.o

obj-$(CONFIG_X86_32)		+= pgtable_32.o iomap_32.o

obj-$(CONFIG_HUGETLB_PAGE)	+= hugetlbpage.o
obj-$(CONFIG_X86_PTDUMP_CORE)	+= dump_pagetables.o
obj-$(CONFIG_X86_PTDUMP)	+= debug_pagetables.o

obj-$(CONFIG_HIGHMEM)		+= highmem_32.o

KASAN_SANITIZE_kasan_init_$(BITS).o := n
obj-$(CONFIG_KASAN)		+= kasan_init_$(BITS).o

obj-$(CONFIG_MMIOTRACE)		+= mmiotrace.o
mmiotrace-y			:= kmmio.o pf_in.o mmio-mod.o
obj-$(CONFIG_MMIOTRACE_TEST)	+= testmmiotrace.o

obj-$(CONFIG_NUMA)		+= numa.o numa_$(BITS).o
obj-$(CONFIG_AMD_NUMA)		+= amdtopology.o
obj-$(CONFIG_ACPI_NUMA)		+= srat.o
obj-$(CONFIG_NUMA_EMU)		+= numa_emulation.o

obj-$(CONFIG_X86_INTEL_MPX)			+= mpx.o
obj-$(CONFIG_X86_INTEL_MEMORY_PROTECTION_KEYS)	+= pkeys.o
obj-$(CONFIG_RANDOMIZE_MEMORY)			+= kaslr.o
obj-$(CONFIG_PAGE_TABLE_ISOLATION)		+= pti.o

obj-$(CONFIG_AMD_MEM_ENCRYPT)	+= mem_encrypt.o
obj-$(CONFIG_AMD_MEM_ENCRYPT)	+= mem_encrypt_identity.o
obj-$(CONFIG_AMD_MEM_ENCRYPT)	+= mem_encrypt_boot.o
