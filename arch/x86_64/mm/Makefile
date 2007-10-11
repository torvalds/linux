#
# Makefile for the linux x86_64-specific parts of the memory manager.
#

obj-y	 := init_64.o fault.o ioremap_64.o extable_64.o pageattr_64.o mmap_64.o
obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o
obj-$(CONFIG_NUMA) += numa_64.o
obj-$(CONFIG_K8_NUMA) += k8topology_64.o
obj-$(CONFIG_ACPI_NUMA) += srat_64.o

hugetlbpage-y = ../../i386/mm/hugetlbpage.o
