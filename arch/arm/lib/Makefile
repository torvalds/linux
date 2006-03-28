#
# linux/arch/arm/lib/Makefile
#
# Copyright (C) 1995-2000 Russell King
#

lib-y		:= backtrace.o changebit.o csumipv6.o csumpartial.o   \
		   csumpartialcopy.o csumpartialcopyuser.o clearbit.o \
		   copy_page.o delay.o findbit.o memchr.o memcpy.o    \
		   memmove.o memset.o memzero.o setbit.o              \
		   strncpy_from_user.o strnlen_user.o                 \
		   strchr.o strrchr.o                                 \
		   testchangebit.o testclearbit.o testsetbit.o        \
		   getuser.o putuser.o clear_user.o                   \
		   ashldi3.o ashrdi3.o lshrdi3.o muldi3.o             \
		   ucmpdi2.o lib1funcs.o div64.o sha1.o               \
		   io-readsb.o io-writesb.o io-readsl.o io-writesl.o

# the code in uaccess.S is not preemption safe and
# probably faster on ARMv3 only
ifeq ($(CONFIG_PREEMPT),y)
  lib-y	+= copy_from_user.o copy_to_user.o
else
ifneq ($(CONFIG_CPU_32v3),y)
  lib-y	+= copy_from_user.o copy_to_user.o
else
  lib-y	+= uaccess.o
endif
endif

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
