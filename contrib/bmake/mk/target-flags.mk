# NAME:
#	target-flags.mk - target specific flags
#
# DESCRIPTION:
#	Include this macro file after all others in a makefile and
#	follow it with any target specific flag settings.
#	For each such variable v in TARGET_FLAG_VARS we set:
#.nf
#
#	_$v := ${$v}
#	$v = ${${v}_${.TARGET:T}:U${_$v}}
#.fi
#
#	This allows one to do things like:
#.nf
#
#	TARGET_FLAG_VARS= CFLAGS
#	.include <target-flags.mk>
#	CFLAGS_fu.o = ${_CFLAGS:N-Wall}
#.fi
#
#	To turn off -Wall for just the target fu.o
#	Actually CFLAGS is the default value for TARGET_FLAG_VARS.
#
# BUGS:
#	One must be careful to avoid creating circular references in
#	variables.  The original version of this macro file did
#	elaborate things with CFLAGS.  The current, simpler
#	implementation is ultimately more flexible.
#	
#	It is important that target-flags.mk is included after other
#	macro files and that target specific flags that may reference
#	_$v are set after that.
#	
#	Only works with a make(1) that does nested evaluation correctly.



# RCSid:
#	$Id: target-flags.mk,v 1.9 2014/04/05 22:56:54 sjg Exp $
#
#	@(#) Copyright (c) 1998-2002, Simon J. Gerraty
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

TARGET_FLAG_VARS?= CFLAGS
.for v in ${TARGET_FLAG_VARS}
.ifndef _$v
_$v := ${$v}
$v  =  ${${v}_${.TARGET:T}:U${_$v}}
.endif
.endfor

