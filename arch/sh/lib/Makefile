#
# Makefile for SuperH-specific library files..
#

lib-y  = delay.o memset.o memmove.o memchr.o \
	 checksum.o strlen.o div64.o div64-generic.o

obj-y				+= io.o

memcpy-y			:= memcpy.o
memcpy-$(CONFIG_CPU_SH4)	:= memcpy-sh4.o

lib-$(CONFIG_MMU)		+= copy_page.o clear_page.o
lib-y				+= $(memcpy-y)

EXTRA_CFLAGS += -Werror
