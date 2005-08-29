#
# Makefile for the linux x86_64-specific parts of the memory manager.
#

obj-y	 := init.o fault.o ioremap.o extable.o pageattr.o
obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o
obj-$(CONFIG_NUMA) += numa.o
obj-$(CONFIG_K8_NUMA) += k8topology.o
obj-$(CONFIG_ACPI_NUMA) += srat.o

hugetlbpage-y = ../../i386/mm/hugetlbpage.o
