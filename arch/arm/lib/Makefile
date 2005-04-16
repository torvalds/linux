#
# linux/arch/arm/lib/Makefile
#
# Copyright (C) 1995-2000 Russell King
#

lib-y		:= backtrace.o changebit.o csumipv6.o csumpartial.o   \
		   csumpartialcopy.o csumpartialcopyuser.o clearbit.o \
		   copy_page.o delay.o findbit.o memchr.o memcpy.o    \
		   memset.o memzero.o setbit.o strncpy_from_user.o    \
		   strnlen_user.o strchr.o strrchr.o testchangebit.o  \
		   testclearbit.o testsetbit.o uaccess.o getuser.o    \
		   putuser.o ashldi3.o ashrdi3.o lshrdi3.o muldi3.o   \
		   ucmpdi2.o udivdi3.o lib1funcs.o div64.o	      \
		   io-readsb.o io-writesb.o io-readsl.o io-writesl.o

ifeq ($(CONFIG_CPU_32v3),y)
  lib-y	+= io-readsw-armv3.o io-writesw-armv3.o
else
  lib-y	+= io-readsw-armv4.o io-writesw-armv4.o
endif

lib-$(CONFIG_ARCH_RPC)		+= ecard.o io-acorn.o floppydma.o
lib-$(CONFIG_ARCH_CLPS7500)	+= io-acorn.o
lib-$(CONFIG_ARCH_L7200)	+= io-acorn.o
lib-$(CONFIG_ARCH_SHARK)	+= io-shark.o

$(obj)/csumpartialcopy.o:	$(obj)/csumpartialcopygeneric.S
$(obj)/csumpartialcopyuser.o:	$(obj)/csumpartialcopygeneric.S
