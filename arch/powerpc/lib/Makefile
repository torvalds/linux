#
# Makefile for ppc-specific library files..
#

ifeq ($(CONFIG_PPC64),y)
EXTRA_CFLAGS		+= -mno-minimal-toc
endif

ifeq ($(CONFIG_PPC_MERGE),y)
obj-y			:= string.o strcase.o
obj-$(CONFIG_PPC32)	+= div64.o copy_32.o checksum_32.o
endif

obj-$(CONFIG_PPC64)	+= checksum_64.o copypage_64.o copyuser_64.o \
			   memcpy_64.o usercopy_64.o mem_64.o string.o \
			   strcase.o
obj-$(CONFIG_PPC_ISERIES) += e2a.o
obj-$(CONFIG_XMON)	+= sstep.o

ifeq ($(CONFIG_PPC64),y)
obj-$(CONFIG_SMP)	+= locks.o
obj-$(CONFIG_DEBUG_KERNEL) += sstep.o
endif
