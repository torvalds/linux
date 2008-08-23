obj-y	:=  init_$(BITS).o fault.o ioremap.o extable.o pageattr.o mmap.o \
	    pat.o pgtable.o gup.o

obj-$(CONFIG_X86_32)		+= pgtable_32.o

obj-$(CONFIG_HUGETLB_PAGE)	+= hugetlbpage.o
obj-$(CONFIG_X86_PTDUMP)	+= dump_pagetables.o

obj-$(CONFIG_HIGHMEM)		+= highmem_32.o

obj-$(CONFIG_MMIOTRACE_HOOKS)	+= kmmio.o
obj-$(CONFIG_MMIOTRACE)		+= mmiotrace.o
mmiotrace-y			:= pf_in.o mmio-mod.o
obj-$(CONFIG_MMIOTRACE_TEST)	+= testmmiotrace.o

ifeq ($(CONFIG_X86_32),y)
obj-$(CONFIG_NUMA)		+= discontig_32.o
else
obj-$(CONFIG_NUMA)		+= numa_64.o
obj-$(CONFIG_K8_NUMA)		+= k8topology_64.o
endif
obj-$(CONFIG_ACPI_NUMA)		+= srat_$(BITS).o

obj-$(CONFIG_MEMTEST)		+= memtest.o
