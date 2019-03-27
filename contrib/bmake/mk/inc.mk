# $Id: inc.mk,v 1.7 2017/05/06 17:29:45 sjg Exp $
#
#	@(#) Copyright (c) 2008, Simon J. Gerraty
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

.include <init.mk>

.if !empty(LIBOWN)
INC_INSTALL_OWN ?= -o ${LIBOWN} -g ${LIBGRP}
.endif
INCMODE ?= 444
INC_COPY ?= -C
INCSDIR ?= ${INCDIR}

STAGE_INCSDIR?= ${STAGE_OBJTOP}${INCSDIR}

# accommodate folk used to freebsd
INCGROUPS ?= ${INCSGROUPS:UINCS}
INCGROUPS := ${INCGROUPS:O:u}

.if !target(buildincludes)
.for group in ${INCGROUPS}
buildincludes: ${${group}}
.endfor
.endif
buildincludes:
includes: buildincludes

.if !target(incinstall)
.for group in ${INCGROUPS}
.if !empty(${group})
.if ${group} != "INC"
${group}_INSTALL_OWN ?= ${INC_INSTALL_OWN}
${group}DIR ?= ${INCDIR}
.endif
# incase we are staging
STAGE_DIR.${group} ?= ${STAGE_OBJTOP}${${group}DIR}

.for header in ${${group}:O:u}
${group}_INSTALL_OWN.${header:T} ?= ${${group}_INSTALL_OWN}
${group}DIR.${header:T} ?= ${${group}DIR}
inc_mkdir_list += ${${group}DIR.${header:T}}

.if defined(${group}NAME.${header:T})
STAGE_AS_SETS += ${group}
STAGE_AS_${header} = ${${group}NAME.${header:T}}
stage_as.${group}: ${header}

incinstall: incinstall.${group}.${header:T}
incinstall.${group}.${header:T}: ${header} inc_mkdirs
	${INSTALL} ${INC_COPY} ${${group}_INSTALL_OWN.${header:T}} -m ${INCMODE} ${.ALLSRC:Ninc_mkdirs} ${DESTDIR}${${group}DIR}/${${group}NAME.${header:T}}

.else
STAGE_SETS += ${group}
stage_files.${group}: ${header}
incinstall.${group}: ${header}
incinstall: incinstall.${group}
.endif

.endfor				# header

incinstall.${group}: inc_mkdirs
	${INSTALL} ${INC_COPY} ${${group}_INSTALL_OWN} -m ${INCMODE} \
	${.ALLSRC:Ninc_mkdirs:O:u} ${DESTDIR}${${group}DIR}

.endif				# !empty
.endfor				# group

inc_mkdirs:
	@for d in ${inc_mkdir_list:O:u}; do \
		test -d ${DESTDIR}$$d || \
		${INSTALL} -d ${INC_INSTALL_OWN} -m 775 ${DESTDIR}$$d; \
	done

.endif				# !target(incinstall)

beforeinstall:
realinstall:	incinstall
.ORDER: beforeinstall incinstall
