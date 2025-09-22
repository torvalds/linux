#	$OpenBSD: Makefile.inc,v 1.8 2016/05/26 16:29:51 deraadt Exp $

M=${.CURDIR:C/^.*\/etc\.([^\/]*)$/\1/}

DOCDIR=	${.CURDIR}/../../share/man/man8/man8.${M}

MAKEDEV: ${.CURDIR}/MAKEDEV ${DOCDIR}/MAKEDEV.8

# set 'NOCLEAN=:' to debug

MAKEDEVSRC=	${.CURDIR}/../MAKEDEV.mi
MAKEDEVSRC+=	${.CURDIR}/../MAKEDEV.sub
MAKEDEVSRC+=	${.CURDIR}/../MAKEDEV.common
MAKEDEVSRC+=	MAKEDEV.md

${.CURDIR}/MAKEDEV: ${MAKEDEVSRC}
	@if m4 -I${.CURDIR}/.. -I${.CURDIR} \
	    ${.CURDIR}/../MAKEDEV.mi > MAKEDEV.tmp; then \
	    if ! [ -f ${.CURDIR}/MAKEDEV ]; then \
		echo "    m4 -I.. -I. ../MAKEDEV.mi > MAKEDEV"; \
		cp MAKEDEV.tmp ${.CURDIR}/MAKEDEV; \
		${NOCLEAN} rm MAKEDEV.tmp; \
		exit 0; \
	    fi; \
	    diff MAKEDEV.tmp ${.CURDIR}/MAKEDEV > diffout1 || true; \
	    sed -e 's/^--.*$$//' \
		-e 's/^[\<\>] #.*OpenBSD.*Exp $$//' \
		-e 's/^[0-9,]*c[0-9,]*$$//' \
		-e '/^$$/d' \
		diffout1 > diffout2; \
	    if [ -s diffout2 ]; then \
		echo "    m4 -I.. -I. ../MAKEDEV.mi > MAKEDEV"; \
		cp MAKEDEV.tmp ${.CURDIR}/MAKEDEV || true; \
		${NOCLEAN} rm MAKEDEV.tmp; \
	    fi; \
	    ${NOCLEAN} rm -f diffout1 diffout2; \
	fi; ${NOCLEAN} rm -f MAKEDEV.tmp

MAKEDEVDOC=	${.CURDIR}/../MAKEDEV.man
MAKEDEVDOC+=	${.CURDIR}/../MAKEDEV.mansub
MAKEDEVDOC+=	${.CURDIR}/../MAKEDEV.common
MAKEDEVDOC+=	MAKEDEV.md

${DOCDIR}/MAKEDEV.8: ${MAKEDEVDOC}
	@if m4 -I${.CURDIR}/.. -I${.CURDIR} \
	    ${.CURDIR}/../MAKEDEV.man > MAKEDEV.mtmp; then \
	    if ! [ -f ${DOCDIR}/MAKEDEV.8 ]; then \
		echo "    m4 -I.. -I. ../MAKEDEV.man > ${DOCDIR}/MAKEDEV.8"; \
		cp MAKEDEV.mtmp ${DOCDIR}/MAKEDEV.8; \
		${NOCLEAN} rm MAKEDEV.mtmp; \
		exit 0; \
	    fi; \
	    diff MAKEDEV.mtmp ${DOCDIR}/MAKEDEV.8 > mdiffout1 || true; \
	    sed -e 's/^--.*$$//' \
		-e 's/^[\<\>] \.\\".*OpenBSD.*Exp $$//' \
		-e 's/^[\<\>] \.\\".*\$$OpenBSD.*\$$$$//' \
		-e 's/^[\<\>] \.Dd \$$Mdocdate.*\$$$$//' \
		-e 's/^[\<\>] \.Dd .*[0-9][0-9][0-9][0-9]$$//' \
		-e 's/^[0-9,]*c[0-9,]*$$//' \
		-e '/^$$/d' \
		mdiffout1 > mdiffout2; \
	    if [ -s mdiffout2 ]; then \
		echo "    m4 -I.. -I. ../MAKEDEV.man > ${DOCDIR}/MAKEDEV.8"; \
		cp MAKEDEV.mtmp ${DOCDIR}/MAKEDEV.8 || true; \
		${NOCLEAN} rm MAKEDEV.mtmp; \
	    fi; \
	    ${NOCLEAN} rm -f mdiffout1 mdiffout2; \
	fi; ${NOCLEAN} rm -f MAKEDEV.mtmp

.PHONY: ${.CURDIR}/MAKEDEV ${DOCDIR}/MAKEDEV.8
