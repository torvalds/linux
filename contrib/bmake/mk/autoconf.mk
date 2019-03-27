# $Id: autoconf.mk,v 1.9 2017/08/13 20:03:13 sjg Exp $
#
#	@(#) Copyright (c) 1996-2009, Simon J. Gerraty
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

.NOPATH:	config.h config.status

CONFIGURE_DEPS += ${.CURDIR}/config.h.in ${.CURDIR}/configure

.if !target(config.h)
config.h:	${CONFIGURE_DEPS} config.status
	./config.status
.endif

.if !target(config.status)
# avoid the targets behaving differently
.if exists(${.OBJDIR}/config.status)
config.status:	config.recheck
.else
config.status:  config.gen
.endif

config.recheck: ${CONFIGURE_DEPS}
	./config.status --recheck
	@touch $@

config.gen: ${CONFIGURE_DEPS}
	CC="${CC} ${CCMODE}" ${.CURDIR}/configure --no-create ${CONFIGURE_ARGS}
	@touch $@ config.recheck

CLEANFILES+= config.recheck config.gen config.status *.meta
.endif

# avoid things blowing up if these are not here...
# this is not quite per the autoconf manual,
# and is extremely convoluted - but all utterly necessary!

.if make(autoconf-in) || make(configure) || make(config.h.in) || ${AUTO_AUTOCONF:Uno:tl} == "yes"
AUTOCONF ?= autoconf
AUTOHEADER ?= autoheader

# expand it to a full path 
AUTOCONF := ${AUTOCONF:${M_whence}}

.if exists(${AUTOCONF})

.PRECIOUS: configure config.h.in config.status

ACLOCAL =
ACCONFIG =

.if exists(${.CURDIR}/aclocal.m4)
ACLOCAL += aclocal.m4
.endif
# use of acconfig.h is deprecated!
.if exists(${.CURDIR}/acconfig.h)
ACCONFIG += acconfig.h
.endif

config.h.in:	${.CURDIR}/configure.in ${ACCONFIG}
	(cd ${.CURDIR} && ${AUTOHEADER})

configure:	${.CURDIR}/configure.in ${ACLOCAL}
	(cd ${.CURDIR} && ${AUTOCONF})

AUTOCONF_INPUTS += configure
autoconf-input:	${AUTOCONF_INPUTS}

.endif
.endif
