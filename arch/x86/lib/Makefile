#
# Makefile for x86 specific library files.
#

obj-$(CONFIG_SMP) := msr-on-cpu.o

lib-y := delay_$(BITS).o
lib-y += usercopy_$(BITS).o getuser_$(BITS).o putuser_$(BITS).o
lib-y += memcpy_$(BITS).o

ifeq ($(CONFIG_X86_32),y)
        lib-y += checksum_32.o
        lib-y += strstr_32.o
        lib-y += bitops_32.o semaphore_32.o string_32.o

        lib-$(CONFIG_X86_USE_3DNOW) += mmx_32.o
else
        obj-y += io_64.o iomap_copy_64.o

        CFLAGS_csum-partial_64.o := -funroll-loops

        lib-y += csum-partial_64.o csum-copy_64.o csum-wrappers_64.o
        lib-y += thunk_64.o clear_page_64.o copy_page_64.o
        lib-y += bitstr_64.o bitops_64.o
        lib-y += memmove_64.o memset_64.o
        lib-y += copy_user_64.o rwlock_64.o copy_user_nocache_64.o
endif
