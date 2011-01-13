#
# Makefile for SuperH-specific library files..
#

lib-y  = delay.o memmove.o memchr.o \
	 checksum.o strlen.o div64.o div64-generic.o

# Extracted from libgcc
lib-y += movmem.o ashldi3.o ashrdi3.o lshrdi3.o \
	 ashlsi3.o ashrsi3.o ashiftrt.o lshrsi3.o \
	 udiv_qrnnd.o

udivsi3-y			:= udivsi3_i4i-Os.o

ifneq ($(CONFIG_CC_OPTIMIZE_FOR_SIZE),y)
udivsi3-$(CONFIG_CPU_SH3)	:= udivsi3_i4i.o
udivsi3-$(CONFIG_CPU_SH4)	:= udivsi3_i4i.o
endif
udivsi3-y			+= udivsi3.o

obj-y				+= io.o

memcpy-y			:= memcpy.o
memcpy-$(CONFIG_CPU_SH4)	:= memcpy-sh4.o

memset-y			:= memset.o
memset-$(CONFIG_CPU_SH4)	:= memset-sh4.o

lib-$(CONFIG_MMU)		+= copy_page.o __clear_user.o
lib-$(CONFIG_MCOUNT)		+= mcount.o
lib-y				+= $(memcpy-y) $(memset-y) $(udivsi3-y)

ccflags-y := -Werror
