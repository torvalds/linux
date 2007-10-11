#
# Makefile for the linux x86_64-specific parts of the memory manager.
#

obj-y	 := init_64.o fault.o ioremap_64.o extable.o pageattr.o mmap.o
obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o
obj-$(CONFIG_NUMA) += numa.o
obj-$(CONFIG_K8_NUMA) += k8topology.o
obj-$(CONFIG_ACPI_NUMA) += srat_64.o

hugetlbpage-y = ../../i386/mm/hugetlbpage.o
