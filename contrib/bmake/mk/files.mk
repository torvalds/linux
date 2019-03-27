# $Id: files.mk,v 1.6 2017/05/07 02:21:02 sjg Exp $
#
#	@(#) Copyright (c) 2017, Simon J. Gerraty
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

FILES_INSTALL_OWN ?= -o ${SHAREOWN} -g ${SHAREGRP}
FILESMODE ?= ${SHAREMODE}
FILES_COPY ?= -C

FILESGROUPS ?= FILES
FILESGROUPS := ${FILESGROUPS:O:u}

.if !target(buildfiles)
.for group in ${FILESGROUPS}
buildfiles: ${${group}}
.endfor
.endif
buildfiles:
realbuild: buildfiles

# there is no default FILESDIR so
# ignore group if ${group}DIR is not defined
.for group in ${FILESGROUPS}
.if !empty(${group}) && defined(${group}DIR)
.if ${group} != "FILES"
${group}_INSTALL_OWN ?= ${FILES_INSTALL_OWN}
.endif
# incase we are staging
STAGE_DIR.${group} ?= ${STAGE_OBJTOP}${${group}DIR}

.for file in ${${group}:O:u}
${group}_INSTALL_OWN.${file:T} ?= ${${group}_INSTALL_OWN}
${group}DIR.${file:T} ?= ${${group}DIR}
file_mkdir_list += ${${group}DIR.${file:T}}

.if defined(${group}NAME.${file:T})
STAGE_AS_SETS += ${group}
STAGE_AS_${file} = ${${group}NAME.${file:T}}
stage_as.${group}: ${file}

installfiles: installfiles.${group}.${file:T}
installfiles.${group}.${file:T}: ${file} file_mkdirs
	${INSTALL} ${FILES_COPY} ${${group}_INSTALL_OWN.${file:T}} \
	-m ${FILESMODE} ${.ALLSRC:Nfile_mkdirs} ${DESTDIR}${${group}DIR}/${${group}NAME.${file:T}}

.else
STAGE_SETS += ${group}
stage_files.${group}: ${file}
installfiles.${group}: ${file}
installfiles: installfiles.${group}
.endif

.endfor				# file

installfiles.${group}: file_mkdirs
	${INSTALL} ${FILES_COPY} ${${group}_INSTALL_OWN} -m ${FILESMODE} \
	${.ALLSRC:Nfile_mkdirs:O:u} ${DESTDIR}${${group}DIR}

.endif				# !empty
.endfor				# group

file_mkdirs:
	@for d in ${file_mkdir_list:O:u}; do \
		test -d ${DESTDIR}$$d || \
		${INSTALL} -d ${FILES_INSTALL_OWN} -m 775 ${DESTDIR}$$d; \
	done

beforeinstall:
installfiles:
realinstall:	installfiles
.ORDER: beforeinstall installfiles
