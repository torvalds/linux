# $Id: prlist.mk,v 1.3 2008/07/17 16:24:57 sjg Exp $
#
#	@(#) Copyright (c) 2006, Simon J. Gerraty
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

.if !target(__${.PARSEFILE}__)
__${.PARSEFILE}__:

# this needs to be included after all the lists it will process
# are defined - which is why it is a separate file.
# Usage looks like:
#   MAKEFLAGS= ${.MAKE} -f ${MAKEFILE} prlist.SOMETHING_HUGE | xargs whatever
#
.if make(prlist.*)
.for t in ${.TARGETS:Mprlist.*:E}
.if empty($t)
prlist.$t:
.else
prlist.$t:	${$t:O:u:S,^,prlist-,}
${$t:O:u:S,^,prlist-,}: .PHONY
	@echo "${.TARGET:S,prlist-,,}"
.endif
.endfor
.endif

.endif
