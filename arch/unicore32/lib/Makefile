# SPDX-License-Identifier: GPL-2.0
#
# linux/arch/unicore32/lib/Makefile
#
# Copyright (C) 2001-2010 GUAN Xue-tao
#

lib-y	:= backtrace.o delay.o findbit.o
lib-y	+= strncpy_from_user.o strnlen_user.o
lib-y	+= clear_user.o copy_page.o
lib-y	+= copy_from_user.o copy_to_user.o

GNU_LIBC_A		:= $(shell $(CC) $(KBUILD_CFLAGS) -print-file-name=libc.a)
GNU_LIBC_A_OBJS		:= memchr.o memcpy.o memmove.o memset.o
GNU_LIBC_A_OBJS		+= strchr.o strrchr.o
GNU_LIBC_A_OBJS		+= rawmemchr.o			# needed by strrchr.o

GNU_LIBGCC_A		:= $(shell $(CC) $(KBUILD_CFLAGS) -print-file-name=libgcc.a)
GNU_LIBGCC_A_OBJS	:= _ashldi3.o _ashrdi3.o _lshrdi3.o
GNU_LIBGCC_A_OBJS	+= _divsi3.o _modsi3.o _ucmpdi2.o _umodsi3.o _udivsi3.o

lib-y	+= $(GNU_LIBC_A_OBJS) $(GNU_LIBGCC_A_OBJS)

$(addprefix $(obj)/, $(GNU_LIBC_A_OBJS)):
	$(Q)$(AR) p $(GNU_LIBC_A) $(notdir $@) > $@

$(addprefix $(obj)/, $(GNU_LIBGCC_A_OBJS)):
	$(Q)$(AR) p $(GNU_LIBGCC_A) $(notdir $@) > $@
