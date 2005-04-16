#
# Makefile for SuperH-specific library files..
#

lib-y  = delay.o memset.o memmove.o memchr.o \
	 checksum.o strcasecmp.o strlen.o div64.o udivdi3.o \
	 div64-generic.o

memcpy-y			:= memcpy.o
memcpy-$(CONFIG_CPU_SH4)	:= memcpy-sh4.o

lib-y	+= $(memcpy-y)

