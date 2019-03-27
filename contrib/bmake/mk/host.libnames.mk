# $Id: host.libnames.mk,v 1.4 2010/01/11 23:01:31 sjg Exp $
#
#	@(#) Copyright (c) 2007-2009, Simon J. Gerraty
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


DLIBEXT ?= .a
DSHLIBEXT ?= ${DLIBEXT}
HOST_LIBEXT ?= ${DSHLIBEXT}
HOST_LIBDIRS ?= /usr/lib /lib
HOST_LIBS ?=

.for x in ${HOST_LIBS:O:u}
.for d in ${HOST_LIBDIRS}
.if exists($d/lib$x${HOST_LIBEXT})
LIB${x:tu} ?= $d/lib$x${HOST_LIBEXT}
.endif
.endfor
.endfor
