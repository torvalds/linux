#
# Makefile for MIPS-specific library files..
#

lib-y	+= bitops.o csum_partial.o delay.o memcpy.o memset.o \
	   mips-atomic.o strncpy_user.o \
	   strnlen_user.o uncached.o

obj-y			+= iomap.o
obj-$(CONFIG_PCI)	+= iomap-pci.o
lib-$(CONFIG_GENERIC_CSUM)	:= $(filter-out csum_partial.o, $(lib-y))

obj-$(CONFIG_CPU_GENERIC_DUMP_TLB) += dump_tlb.o
obj-$(CONFIG_CPU_R3000)		+= r3k_dump_tlb.o
obj-$(CONFIG_CPU_TX39XX)	+= r3k_dump_tlb.o

# libgcc-style stuff needed in the kernel
obj-y += ashldi3.o ashrdi3.o bswapsi.o bswapdi.o cmpdi2.o lshrdi3.o ucmpdi2.o
