#
# Makefile for ppc-specific library files..
#

obj-y			:= strcase.o string.o
obj-$(CONFIG_PPC32)	+= div64.o copy32.o checksum.o
obj-$(CONFIG_PPC64)	+= copypage.o copyuser.o memcpy.o usercopy.o \
			   sstep.o checksum64.o
obj-$(CONFIG_PPC_ISERIES) += e2a.o
