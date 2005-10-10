#
# Makefile for ppc-specific library files..
#

obj-y			:= strcase.o string.o
obj-$(CONFIG_PPC32)	+= div64.o copy_32.o checksum_32.o
obj-$(CONFIG_PPC64)	+= copypage_64.o copyuser_64.o memcpy_64.o \
			   usercopy_64.o sstep.o checksum_64.o mem_64.o
obj-$(CONFIG_PPC_ISERIES) += e2a.o
