#	$Id: subdir.mk,v 1.16 2017/02/08 22:16:59 sjg Exp $
#	skip missing directories...

#	$NetBSD: bsd.subdir.mk,v 1.11 1996/04/04 02:05:06 jtc Exp $
#	@(#)bsd.subdir.mk	5.9 (Berkeley) 2/1/91

.if ${.MAKE.LEVEL} == 0 && ${.MAKE.MODE:Uno:Mmeta*} != ""
.include <meta.subdir.mk>
# keep everyone happy
_SUBDIRUSE:
.elif !commands(_SUBDIRUSE) && !defined(NO_SUBDIR) && !defined(NOSUBDIR)
.-include <${.CURDIR}/Makefile.inc>
.if !target(.MAIN)
.MAIN: all
.endif

ECHO_DIR ?= echo
.ifdef SUBDIR_MUST_EXIST
MISSING_DIR=echo "Missing ===> ${.CURDIR}/$${entry}"; exit 1
.else
MISSING_DIR=echo "Skipping ===> ${.CURDIR}/$${entry}"; continue
.endif

_SUBDIRUSE: .USE
.if defined(SUBDIR)
	@Exists() { test -f $$1; }; \
	for entry in ${SUBDIR}; do \
		(set -e; \
		if Exists ${.CURDIR}/$${entry}.${MACHINE}/[mM]akefile; then \
			_newdir_="$${entry}.${MACHINE}"; \
		elif  Exists ${.CURDIR}/$${entry}/[mM]akefile; then \
			_newdir_="$${entry}"; \
		else \
			${MISSING_DIR}; \
		fi; \
		if test X"${_THISDIR_}" = X""; then \
			_nextdir_="$${_newdir_}"; \
		else \
			_nextdir_="$${_THISDIR_}/$${_newdir_}"; \
		fi; \
		${ECHO_DIR} "===> $${_nextdir_}"; \
		cd ${.CURDIR}/$${_newdir_}; \
		${.MAKE} _THISDIR_="$${_nextdir_}" \
		    ${.TARGET:S/realinstall/install/:S/.depend/depend/}) || exit 1; \
	done

${SUBDIR}::
	@set -e; _r=${.CURDIR}/; \
	if test -z "${.TARGET:M/*}"; then \
		if test -d ${.CURDIR}/${.TARGET}.${MACHINE}; then \
			_newdir_=${.TARGET}.${MACHINE}; \
		else \
			_newdir_=${.TARGET}; \
		fi; \
	else \
		_r= _newdir_=${.TARGET}; \
	fi; \
	${ECHO_DIR} "===> $${_newdir_}"; \
	cd $${_r}$${_newdir_}; \
	${.MAKE} _THISDIR_="$${_newdir_}" all
.endif

.if !target(install)
.if !target(beforeinstall)
beforeinstall:
.endif
.if !target(afterinstall)
afterinstall:
.endif
install: maninstall
maninstall: afterinstall
afterinstall: realinstall
realinstall: beforeinstall _SUBDIRUSE
.endif

.if defined(SRCS)
etags: ${SRCS}
	-cd ${.CURDIR}; etags `echo ${.ALLSRC:N*.h} | sed 's;${.CURDIR}/;;'`
.endif

SUBDIR_TARGETS += \
	all \
	clean \
	cleandir \
	includes \
	depend \
	lint \
	obj \
	tags \
	etags

.for t in ${SUBDIR_TARGETS:O:u}
$t: _SUBDIRUSE
.endfor

.include <own.mk>
.if make(destroy*)
.include <obj.mk>
.endif
.endif
# make sure this exists
all:
