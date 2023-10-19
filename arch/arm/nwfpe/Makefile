# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 1998, 1999, 2001 Philip Blundell
#

obj-$(CONFIG_FPE_NWFPE)		+= nwfpe.o

nwfpe-y				+= fpa11.o fpa11_cpdo.o fpa11_cpdt.o \
				   fpa11_cprt.o fpmodule.o fpopcode.o \
				   softfloat.o single_cpdo.o double_cpdo.o \
				   entry.o

nwfpe-$(CONFIG_FPE_NWFPE_XP)	+= extended_cpdo.o

# Try really hard to avoid generating calls to __aeabi_uldivmod() from
# float64_rem() due to loop elision.
ifdef CONFIG_CC_IS_CLANG
CFLAGS_softfloat.o	+= -mllvm -replexitval=never
endif
