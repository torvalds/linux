
#
# Makefile for m68k-specific library files..
#

lib-y	:= ashldi3.o ashrdi3.o lshrdi3.o muldi3.o \
	   memcpy.o memset.o memmove.o

ifdef CONFIG_MMU
lib-y	+= string.o uaccess.o checksum_mm.o
else
lib-y	+= mulsi3.o divsi3.o udivsi3.o modsi3.o umodsi3.o checksum_no.o
endif

