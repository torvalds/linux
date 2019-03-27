# $Id: scripts.mk,v 1.3 2017/05/06 17:29:45 sjg Exp $
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

.include <init.mk>

SCRIPTSGROUPS ?= SCRIPTS
SCRIPTSGROUPS := ${SCRIPTSGROUPS:O:u}

SCRIPTSDIR?=	${BINDIR}
SCRIPTSOWN?=	${BINOWN}
SCRIPTSGRP?=	${BINGRP}
SCRIPTSMODE?=	${BINMODE}

SCRIPTS_INSTALL_OWN?= -o ${SCRIPTSOWN} -g ${SCRIPTSGRP}
SCRIPTS_COPY ?= -C

# how we get script name from src
SCRIPTSNAME_MOD?=T:R

.if !target(buildfiles)
.for group in ${SCRIPTSGROUPS}
buildfiles: ${${group}}
.endfor
.endif
buildfiles:
realbuild: buildfiles

.for group in ${SCRIPTSGROUPS}
.if !empty(${group}) && defined(${group}DIR)
.if ${group} != "SCRIPTS"
${group}_INSTALL_OWN ?= ${SCRIPTS_INSTALL_OWN}
.endif
# incase we are staging
STAGE_DIR.${group} ?= ${STAGE_OBJTOP}${${group}DIR}

.for script in ${${group}:O:u}
${group}_INSTALL_OWN.${script:T} ?= ${${group}_INSTALL_OWN}
${group}DIR.${script:T} ?= ${${group}DIR_${script:T}:U${${group}DIR}}
script_mkdir_list += ${${group}DIR.${script:T}}

${group}NAME.${script} ?= ${${group}NAME_${script:T}:U${script:${SCRIPTSNAME_MOD}}}
.if ${${group}NAME.${script}:T} != ${script:T}
STAGE_AS_SETS += ${group}
STAGE_AS_${script} = ${${group}NAME.${script:T}}
stage_as.${group}: ${script}

installscripts: installscripts.${group}.${script:T}
installscripts.${group}.${script:T}: ${script} script_mkdirs
	${INSTALL} ${SCRIPTS_COPY} ${${group}_INSTALL_OWN.${script:T}} \
	-m ${SCRIPTSMODE} ${.ALLSRC:Nscript_mkdirs} ${DESTDIR}${${group}DIR}/${${group}NAME.${script:T}}

.else
STAGE_SETS += ${group}
stage_files.${group}: ${script}
installscripts.${group}: ${script}
installscripts: installscripts.${group}
.endif

.endfor				# script

installscripts.${group}: script_mkdirs
	${INSTALL} ${SCRIPTS_COPY} ${${group}_INSTALL_OWN} -m ${SCRIPTSMODE} \
	${.ALLSRC:Nscript_mkdirs:O:u} ${DESTDIR}${${group}DIR}

.endif				# !empty
.endfor				# group

script_mkdirs:
	@for d in ${script_mkdir_list:O:u}; do \
		test -d ${DESTDIR}$$d || \
		${INSTALL} -d ${SCRIPTS_INSTALL_OWN} -m 775 ${DESTDIR}$$d; \
	done


beforeinstall:
installscripts:
realinstall:	installscripts
.ORDER: beforeinstall installscripts

