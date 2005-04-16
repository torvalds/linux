#
# Makefile for ppc64-specific library files..
#

lib-y := checksum.o dec_and_lock.o string.o strcase.o
lib-y += copypage.o memcpy.o copyuser.o usercopy.o

# Lock primitives are defined as no-ops in include/linux/spinlock.h
# for non-SMP configs. Don't build the real versions.

lib-$(CONFIG_SMP) += locks.o

# e2a provides EBCDIC to ASCII conversions.
ifdef CONFIG_PPC_ISERIES
obj-$(CONFIG_PCI)	+= e2a.o
endif

lib-$(CONFIG_DEBUG_KERNEL) += sstep.o
