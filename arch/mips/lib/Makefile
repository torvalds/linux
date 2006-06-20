#
# Makefile for MIPS-specific library files..
#

lib-y	+= csum_partial_copy.o memcpy.o promlib.o strlen_user.o strncpy_user.o \
	   strnlen_user.o uncached.o

obj-y	+= iomap.o

# libgcc-style stuff needed in the kernel
lib-y += ashldi3.o ashrdi3.o lshrdi3.o

EXTRA_AFLAGS := $(CFLAGS)
