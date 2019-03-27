# RCSid:
#	$Id: warnings.mk,v 1.14 2016/04/05 15:58:37 sjg Exp $
#
#	@(#) Copyright (c) 2002, Simon J. Gerraty
#
#	This file is provided in the hope that it will
#	be of use.  There is absolutely NO WARRANTY.
#	Permission to copy, redistribute or otherwise
#	use this file is hereby granted provided that 
#	the above copyright notice and this notice are
#	left intact. 
#      
#	Please send copies of changes and bug-fixes to:
#	sjg@crufty.net
#

.ifndef _w_cflags
# make sure we get the behavior we expect
.MAKE.SAVE_DOLLARS = no

# Any number of warnings sets can be added.
.-include <warnings-sets.mk>

# Modest defaults - put more elaborate sets in warnings-sets.mk
# -Wunused  etc are here so you can set
# W_unused=-Wno-unused etc.
MIN_WARNINGS?= -Wall \
	-Wformat \
	-Wimplicit \
	-Wunused \
	-Wuninitialized 

LOW_WARNINGS?= ${MIN_WARNINGS} -W -Wstrict-prototypes -Wmissing-prototypes
 
MEDIUM_WARNINGS?= ${LOW_WARNINGS} -Werror

HIGH_WARNINGS?= ${MEDIUM_WARNINGS} \
	-Wcast-align \
	-Wcast-qual \
	-Wparentheses \
	-Wpointer-arith \
	-Wmissing-declarations \
	-Wreturn-type \
	-Wswitch \
	-Wwrite-strings

EXTRA_WARNINGS?= ${HIGH_WARNINGS} -Wextra

# The two step default makes it easier to test build with different defaults.
DEFAULT_WARNINGS_SET?= MIN
WARNINGS_SET?= ${DEFAULT_WARNINGS_SET}

# There is always someone who wants more...
.if !empty(WARNINGS_XTRAS)
${WARNINGS_SET}_WARNINGS += ${WARNINGS_XTRAS}
.endif

# If you add sets, besure to list them (you don't have to touch this list).
ALL_WARNINGS_SETS+= MIN LOW MEDIUM HIGH EXTRA

.if !empty(WARNINGS_SET)
.for ws in ${WARNINGS_SET}
.if empty(${ws}_WARNINGS)
.if ${MAKE_VERSION:[1]:C/.*-//} >= 20050530
.BEGIN:	_empty_warnings
_empty_warnings: .PHONY
.else
.BEGIN:
.endif
	@echo "ERROR: Invalid: WARNINGS_SET=${ws}"
	@echo "ERROR: Try one of: ${ALL_WARNINGS_SETS:O:u}"; exit 1

.endif
.endfor
.endif

# Without -O or if we've set -O0 somewhere - to make debugging more effective,
# we need to turn off -Wuninitialized as otherwise we get a warning that
# -Werror turns into an error.  To be safe, set W_uninitialized blank.
_w_cflags= ${CFLAGS} ${CFLAGS_LAST} ${CPPFLAGS}
.if ${_w_cflags:M-O*} == "" || ${_w_cflags:M-O0} != ""
W_uninitialized=
.endif


# .for loops have the [dis]advantage of being evaluated when read,
# so adding to WARNINGS_SET[_${MACHINE_ARCH}] after this file is 
# read has no effect.
# Replacing the above .for loops with the WARNINGS+= below solves that
# but tiggers a double free bug in bmake-20040118 and earlier.
# Don't try and read this too fast!
#
# The first :@ "loop" handles multiple sets in WARNINGS_SET
#
# In the second :@ "loop", the ::?= noise sets W_foo?=-Wfoo etc
# which makes it easy to turn off override individual flags
# (see W_uninitialized above).
# 
# The last bit expands to ${W_foo_${.TARGET:T}:U${W_foo}}
# which is the bit we ultimately want.  It allows W_* to be set on a
# per target basis.
# 
# NOTE: that we force the target extension to be .o
#

# define this once, we use it a couple of times below (hence the doubled $$).
M_warnings_list = @s@$${$$s_WARNINGS}@:O:u:@w@$${$${w:C/-(.)/\1_/}::?=$$w} $${$${w:C/-(.)/\1_/}_${MACHINE_ARCH}_${.TARGET:T:R}.o:U$${$${w:C/-(.)/\1_/}_${.TARGET:T:R}.o:U$${$${w:C/-(.)/\1_/}_${MACHINE_ARCH}:U$${$${w:C/-(.)/\1_/}}}}}@

# first a list of warnings from the chosen set
_warnings = ${WARNINGS_SET_${MACHINE_ARCH}:U${WARNINGS_SET}:${M_warnings_list}}
# now a list of all -Wno-* overrides not just those defined by WARNINGS_SET
# since things like -Wall imply lots of others.
# this should be a super-set of the -Wno-* in _warnings, but 
# just in case...
_no_warnings = ${_warnings:M-Wno-*} ${ALL_WARNINGS_SETS:${M_warnings_list}:M-Wno-*}
# -Wno-* must follow any others
WARNINGS += ${_warnings:N-Wno-*} ${_no_warnings:O:u}

.ifndef NO_CFLAGS_WARNINGS
# Just ${WARNINGS} should do, but this is more flexible?
CFLAGS+= ${WARNINGS_${.TARGET:T:R}.o:U${WARNINGS}}
.endif

# it is rather silly that g++ blows up on some warning flags
NO_CXX_WARNINGS+= \
	implicit \
	missing-declarations \
	missing-prototypes \
	nested-externs \
	shadow \
	strict-prototypes

.for s in ${SRCS:M*.c*:N*.c:N*h}
.for w in ${NO_CXX_WARNINGS}
W_$w_${s:T:R}.o=
.endfor
.endfor

.endif # _w_cflags
