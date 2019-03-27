# $Id: misc.mk,v 1.1.1.1 2014/08/30 18:57:18 sjg Exp $

.if !exists(${.CURDIR}/)
.warning ${.CURDIR}/ doesn't exist ?
.endif

.if !exists(${.CURDIR}/.)
.warning ${.CURDIR}/. doesn't exist ?
.endif

.if !exists(${.CURDIR}/..)
.warning ${.CURDIR}/.. doesn't exist ?
.endif

all:
	@: all is well
