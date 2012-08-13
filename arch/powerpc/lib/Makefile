#
# Makefile for ppc-specific library files..
#

subdir-ccflags-$(CONFIG_PPC_WERROR) := -Werror

ccflags-$(CONFIG_PPC64)	:= -mno-minimal-toc

CFLAGS_REMOVE_code-patching.o = -pg
CFLAGS_REMOVE_feature-fixups.o = -pg

obj-y			:= string.o alloc.o \
			   checksum_$(CONFIG_WORD_SIZE).o crtsavres.o
obj-$(CONFIG_PPC32)	+= div64.o copy_32.o
obj-$(CONFIG_HAS_IOMEM)	+= devres.o

obj-$(CONFIG_PPC64)	+= copypage_64.o copyuser_64.o \
			   memcpy_64.o usercopy_64.o mem_64.o string.o \
			   checksum_wrappers_64.o hweight_64.o \
			   copyuser_power7.o string_64.o copypage_power7.o \
			   memcpy_power7.o
obj-$(CONFIG_XMON)	+= sstep.o ldstfp.o
obj-$(CONFIG_KPROBES)	+= sstep.o ldstfp.o
obj-$(CONFIG_HAVE_HW_BREAKPOINT)	+= sstep.o ldstfp.o

ifeq ($(CONFIG_PPC64),y)
obj-$(CONFIG_SMP)	+= locks.o
obj-$(CONFIG_ALTIVEC)	+= vmx-helper.o
endif

obj-$(CONFIG_PPC_LIB_RHEAP) += rheap.o

obj-y			+= code-patching.o
obj-y			+= feature-fixups.o
obj-$(CONFIG_FTR_FIXUP_SELFTEST) += feature-fixups-test.o
