#
# Makefile for x86_64-specific library files.
#

CFLAGS_csum-partial.o := -funroll-loops

obj-y := io.o iomap_copy_64.o
obj-$(CONFIG_SMP)	+= msr-on-cpu.o

lib-y := csum-partial.o csum-copy_64.o csum-wrappers_64.o delay.o \
	usercopy_64.o getuser_64.o putuser_64.o  \
	thunk_64.o clear_page_64.o copy_page_64.o bitstr_64.o bitops_64.o
lib-y += memcpy_64.o memmove_64.o memset.o copy_user.o rwlock_64.o copy_user_nocache_64.o
