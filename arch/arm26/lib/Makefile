#
# linux/arch/arm26/lib/Makefile
#
# Copyright (C) 1995-2000 Russell King
#

lib-y		:= backtrace.o changebit.o csumipv6.o csumpartial.o   \
		   csumpartialcopy.o csumpartialcopyuser.o clearbit.o \
		   copy_page.o delay.o findbit.o memchr.o memcpy.o    \
		   memset.o memzero.o setbit.o                        \
		   strchr.o strrchr.o testchangebit.o                 \
		   testclearbit.o testsetbit.o getuser.o              \
		   putuser.o ashldi3.o ashrdi3.o lshrdi3.o muldi3.o   \
		   ucmpdi2.o udivdi3.o lib1funcs.o ecard.o io-acorn.o \
		   floppydma.o io-readsb.o io-writesb.o io-writesl.o  \
		   uaccess-kernel.o uaccess-user.o io-readsw.o        \
		   io-writesw.o io-readsl.o ecard.o io-acorn.o        \
		   floppydma.o

lib-n		:=

lib-$(CONFIG_VT)+= kbd.o

csumpartialcopy.o: csumpartialcopygeneric.S
csumpartialcopyuser.o: csumpartialcopygeneric.S

