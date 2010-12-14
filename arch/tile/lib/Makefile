#
# Makefile for TILE-specific library files..
#

lib-y = cacheflush.o checksum.o cpumask.o delay.o \
	mb_incoherent.o uaccess.o memmove.o \
	memcpy_$(BITS).o memchr_$(BITS).o memset_$(BITS).o \
	strchr_$(BITS).o strlen_$(BITS).o

ifeq ($(CONFIG_TILEGX),y)
lib-y += memcpy_user_64.o
else
lib-y += atomic_32.o atomic_asm_32.o memcpy_tile64.o
endif

lib-$(CONFIG_SMP) += spinlock_$(BITS).o usercopy_$(BITS).o

obj-$(CONFIG_MODULES) += exports.o
