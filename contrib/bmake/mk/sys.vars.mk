# $Id: sys.vars.mk,v 1.3 2018/02/06 00:51:53 sjg Exp $
#
#	@(#) Copyright (c) 2003-2009, Simon J. Gerraty
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

# We use the following paradigm for preventing multiple inclusion.
# It relies on the fact that conditionals and dependencies are resolved 
# at the time they are read.
#
# _this ?= ${.PARSEFILE}
# .if !target(__${_this}__)
# __${_this}__:
#
.if ${MAKE_VERSION:U0} > 20100408
_this = ${.PARSEDIR:tA}/${.PARSEFILE}
.else
_this = ${.PARSEDIR}/${.PARSEFILE}
.endif

# if this is an ancient version of bmake
MAKE_VERSION ?= 0
.if ${MAKE_VERSION:M*make-*}
# turn it into what we want - just the date
MAKE_VERSION := ${MAKE_VERSION:[1]:C,.*-,,}
.endif

# some useful modifiers

# A useful trick for testing multiple :M's against something
# :L says to use the variable's name as its value - ie. literal
# got = ${clean* destroy:${M_ListToMatch:S,V,.TARGETS,}}
M_ListToMatch = L:@m@$${V:M$$m}@
# match against our initial targets (see above)
M_L_TARGETS = ${M_ListToMatch:S,V,_TARGETS,}

# turn a list into a set of :N modifiers
# NskipFoo = ${Foo:${M_ListToSkip}}
M_ListToSkip= O:u:S,^,N,:ts:

# type should be a builtin in any sh since about 1980,
# but sadly there are exceptions!
.if ${.MAKE.OS:Unknown:NBSD/OS} == ""
_type_sh = which
.endif

# AUTOCONF := ${autoconf:L:${M_whence}}
M_type = @x@(${_type_sh:Utype} $$x) 2> /dev/null; echo;@:sh:[0]:N* found*:[@]:C,[()],,g
M_whence = ${M_type}:M/*:[1]

# convert a path to a valid shell variable
M_P2V = tu:C,[./-],_,g

# convert path to absolute
.if ${MAKE_VERSION:U0} > 20100408
M_tA = tA
.else
M_tA = C,.*,('cd' & \&\& 'pwd') 2> /dev/null || echo &,:sh
.endif

.if ${MAKE_VERSION:U0} >= 20170130
# M_cmpv allows comparing dotted versions like 3.1.2
# ${3.1.2:L:${M_cmpv}} -> 3001002
# we use big jumps to handle 3 digits per dot:
# ${123.456.789:L:${M_cmpv}} -> 123456789
M_cmpv.units = 1 1000 1000000
M_cmpv = S,., ,g:_:range:@i@+ $${_:[-$$i]} \* $${M_cmpv.units:[$$i]}@:S,^,expr 0 ,1:sh
.endif

# absoulte path to what we are reading.
_PARSEDIR = ${.PARSEDIR:${M_tA}}
