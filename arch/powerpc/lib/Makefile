#
# Makefile for ppc-specific library files..
#

ifeq ($(CONFIG_PPC_MERGE),y)
obj-y			:= strcase.o string.o
obj-$(CONFIG_PPC32)	+= div64.o copy_32.o checksum_32.o
obj-$(CONFIG_PPC64)	+= memcpy_64.o \
			   usercopy_64.o sstep.o mem_64.o
ifeq ($(CONFIG_PPC64),y)
obj-$(CONFIG_SMP)	+= locks.o
endif
endif

obj-$(CONFIG_PPC64)	+= checksum_64.o copypage_64.o copyuser_64.o
obj-$(CONFIG_PPC_ISERIES) += e2a.o
