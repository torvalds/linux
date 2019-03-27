# $Id: sys.clean-env.mk,v 1.22 2017/10/25 23:44:20 sjg Exp $
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

# This makefile would normally be included by sys.env.mk

# The variables used by this makefile include:
#
# MAKE_ENV_SAVE_VAR_LIST
#	The actuall list of variables from the environment that will be
#	preserved.
# MAKE_ENV_SAVE_PREFIX_LIST
#	A list of prefixes to match against the environment - the results
#	are added to MAKE_ENV_SAVE_VAR_LIST after being filtered by...
# MAKE_ENV_SAVE_EXCLUDE_LIST
#	A list of words or patterns which is turned into a list of :N
#	modifiers.

.if ${.MAKE.LEVEL} == 0 && ${MAKE_VERSION} >= 20100606
# We save any env var that starts with the words in MAKE_ENV_SAVE_PREFIX_LIST.
# This gets expanded to an egrep expression like '^(A|B|C...)'
# and added to MAKE_ENV_SAVE_VAR_LIST below.
# If any of these end up being too greedy, MAKE_ENV_SAVE_EXCLUDE_LIST
# can be used to filter.
MAKE_ENV_SAVE_PREFIX_LIST += \
	CCACHE \
	CVS \
	DEBUG \
	DISTCC \
	HOST \
	MACHINE \
	MAKE \
	MK \
	NEED_ \
	SB_ \
	SSH \
	SVN \
	USE_ \
	WITH_ \
	WITHOUT_ \


# This could be a list of vars or patterns to explicitly exclude.
MAKE_ENV_SAVE_EXCLUDE_LIST ?= _

# This is the actual list that we will save
# HOME is probably something worth clobbering eg. 
# HOME=/var/empty
MAKE_ENV_SAVE_VAR_LIST += \
	HOME \
	LOGNAME \
	OBJROOT \
	OBJTOP \
	PATH \
	SB \
	SRCTOP \
	USER \
	${_env_vars:${MAKE_ENV_SAVE_EXCLUDE_LIST:${M_ListToSkip}}}

_env_vars != env | egrep '^(${MAKE_ENV_SAVE_PREFIX_LIST:ts|})' | sed 's,=.*,,'; echo

_export_list =
.for v in ${MAKE_ENV_SAVE_VAR_LIST:O:u}
.if defined($v)
_export_list += $v
# Save current value
$v := ${$v}
.endif
.endfor

# Now, clobber the environment
.unexport-env

# This is a list of vars that we handle specially below
_tricky_env_vars = MAKEOBJDIR OBJTOP
# Export our selection - sans tricky ones
.export ${_export_list:${_tricky_env_vars:${M_ListToSkip}}}

# This next bit may need tweaking
# if you don't happen to like the way I set it.
.if defined(MAKEOBJDIR)
# We are going to set this to the equivalent of the shell's
# MAKEOBJDIR='${.CURDIR:S,${SRCTOP},${OBJTOP},}'
_srctop := ${SRCTOP:U${SB_SRC:U${SB}/src}}
_objroot := ${OBJROOT:U${SB_OBJROOT:U${SB}/${SB_OBJPREFIX}}}
.if ${MAKE_VERSION} < 20160218
_objtop := ${OBJTOP:U${_objroot}${MACHINE}}
# Take care of ${MACHINE}
.if ${MACHINE:Nhost*} == "" || ${OBJTOP} == ${HOST_OBJTOP:Uno}
OBJTOP = ${_objtop:S,${HOST_TARGET}$,\${MACHINE},}
.else
OBJTOP = ${_objtop:S,${MACHINE}$,\${MACHINE},}
.endif
# Export like this
MAKEOBJDIR = $${.CURDIR:S,${_srctop},$${OBJTOP},}
#.info ${MAKE_SAVE_ENV_VARS _srctop _objroot _objtop OBJTOP MAKEOBJDIR:L:@v@${.newline}$v=${$v}@}

# Export these as-is, and do not track...
# otherwise the environment will be ruined when we evaluate them below.
.export-env ${_tricky_env_vars}

# Now evaluate for ourselves
.for v in ${_tricky_env_vars}
$v := ${$v}
.endfor
.else
# we cannot use the '$$' trick, anymore
# but we can export a literal (unexpanded) value
SRCTOP := ${_srctop}
OBJROOT := ${_objroot}
OBJTOP = ${OBJROOT}${MACHINE}
MAKEOBJDIR = ${.CURDIR:S,${SRCTOP},${OBJTOP},}
.export-literal SRCTOP OBJROOT ${_tricky_env_vars}
.endif
#.info ${_tricky_env_vars:@v@${.newline}$v=${$v}@}
#showenv:
#	@env | egrep 'OBJ|SRC'
.endif				# MAKEOBJDIR
.endif				# level 0
