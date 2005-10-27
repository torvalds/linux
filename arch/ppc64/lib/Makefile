#
# Makefile for ppc64-specific library files..
#

lib-y := string.o strcase.o
lib-y += copypage.o memcpy.o copyuser.o usercopy.o

# Lock primitives are defined as no-ops in include/linux/spinlock.h
# for non-SMP configs. Don't build the real versions.

lib-$(CONFIG_SMP) += locks.o

lib-$(CONFIG_DEBUG_KERNEL) += sstep.o
