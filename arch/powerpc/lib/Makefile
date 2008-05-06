#
# Makefile for ppc-specific library files..
#

ifeq ($(CONFIG_PPC64),y)
EXTRA_CFLAGS		+= -mno-minimal-toc
endif

ifeq ($(CONFIG_PPC_MERGE),y)
obj-y			:= string.o alloc.o \
			   checksum_$(CONFIG_WORD_SIZE).o
obj-$(CONFIG_PPC32)	+= div64.o copy_32.o
endif

obj-$(CONFIG_PPC64)	+= copypage_64.o copyuser_64.o \
			   memcpy_64.o usercopy_64.o mem_64.o string.o
obj-$(CONFIG_XMON)	+= sstep.o
obj-$(CONFIG_KPROBES)	+= sstep.o
obj-$(CONFIG_NOT_COHERENT_CACHE)	+= dma-noncoherent.o

ifeq ($(CONFIG_PPC64),y)
obj-$(CONFIG_SMP)	+= locks.o
endif

obj-$(CONFIG_PPC_LIB_RHEAP) += rheap.o
obj-$(CONFIG_HAS_IOMEM)	+= devres.o
