# $Id: sys.debug.mk,v 1.1 2016/10/01 19:11:55 sjg Exp $
#
#	@(#) Copyright (c) 2009, Simon J. Gerraty
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

# Sometimes we want to turn on debugging in just one or two places
# if .CURDIR is matched by any entry in DEBUG_MAKE_SYS_DIRS we
# will apply DEBUG_MAKE_FLAGS now.
# if an entry in DEBUG_MAKE_DIRS matches, we at the end of sys.mk
# eg.  DEBUG_MAKE_FLAGS=-dv DEBUG_MAKE_SYS_DIRS="*lib/sjg"
# use DEBUG_MAKE_FLAGS0 to apply only to .MAKE.LEVEL 0
#
.if ${.MAKE.LEVEL:U1} == 0
# we use indirection, to simplify the tests below, and incase
# DEBUG_* were given on our command line.
_DEBUG_MAKE_FLAGS = ${DEBUG_MAKE_FLAGS0}
_DEBUG_MAKE_SYS_DIRS = ${DEBUG_MAKE_SYS_DIRS0:U${DEBUG_MAKE_SYS_DIRS}}
_DEBUG_MAKE_DIRS = ${DEBUG_MAKE_DIRS0:U${DEBUG_MAKE_DIRS}}
.else
_DEBUG_MAKE_FLAGS = ${DEBUG_MAKE_FLAGS}
_DEBUG_MAKE_SYS_DIRS = ${DEBUG_MAKE_SYS_DIRS}
_DEBUG_MAKE_DIRS = ${DEBUG_MAKE_DIRS}
.endif
