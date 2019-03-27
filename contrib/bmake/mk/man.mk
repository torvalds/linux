# $Id: man.mk,v 1.20 2012/12/13 01:51:01 sjg Exp $

.if !target(__${.PARSEFILE}__)
__${.PARSEFILE}__:

.include <init.mk>

# unlike bsd.man.mk we handle 3 approaches
# 1. install unformated nroff (default)
# 2. install formatted pages
# 3. install formatted pages but with extension of .0
# sadly we cannot rely on a shell that supports ${foo#...} and ${foo%...}
# so we have to use sed(1).

# set MANTARGET=cat for formatted pages
MANTARGET?=	man
# set this to .0 for same behavior as bsd.man.mk
MCATEXT?=

NROFF?=		nroff
MANDIR?=	/usr/share/man
MANDOC?= man

.SUFFIXES: .1 .2 .3 .4 .5 .6 .7 .8 .9 .cat1 .cat2 .cat3 .cat4 .cat5 .cat6 \
	.cat7 .cat8 .cat9

.9.cat9 .8.cat8 .7.cat7 .6.cat6 .5.cat5 .4.cat4 .3.cat3 .2.cat2 .1.cat1:
	@echo "${NROFF} -${MANDOC} ${.IMPSRC} > ${.TARGET:T}"
	@${NROFF} -${MANDOC} ${.IMPSRC} > ${.TARGET:T} || ( rm -f ${.TARGET:T} ; false )

.if defined(MAN) && !empty(MAN)

# we use cmt2doc.pl to extract manpages from source
# this is triggered by the setting of EXTRACT_MAN or MAN being set but
# not existsing.

.if !exists(${MAN:[1]}) && !target(${MAN:[1]})
.if defined(EXTRACT_MAN) && ${EXTRACT_MAN} == "no"
MAN=
.else
.if exists(/usr/local/share/bin/cmt2doc.pl)
CMT2DOC?= cmt2doc.pl
CMT2DOC_OPTS?=  ${CMT2DOC_ORGOPT} -pmS${.TARGET:E}
.endif
.ifdef CMT2DOC
.c.8 .c.5 .c.3 .c.4 .c.1 \
	.cc.8 .cc.5 .cc.3 .cc.4 .cc.1 \
	.h.8 .h.5 .h.3 .h.4 .h.1 \
	.sh.8 .sh.5 .sh.3 .sh.4 .sh.1 \
	.pl.8 .pl.5 .pl.3 .pl.4 .pl.1:
	@echo "${CMT2DOC} ${.IMPSRC} > ${.TARGET:T}"
	@${CMT2DOC} ${CMT2DOC_OPTS} ${.IMPSRC} > ${.TARGET:T} || ( rm -f ${.TARGET:T} ; false )
.else
MAN=
.endif
.endif
.endif

_mandir=${DESTDIR}${MANDIR}/${MANTARGET}`echo $$page | sed -e 's/.*\.cat/./' -e 's/.*\.//'`
.if ${MANTARGET} == "cat"
_mfromdir?=.
MANALL=	${MAN:S/.1$/.cat1/g:S/.2$/.cat2/g:S/.3$/.cat3/g:S/.4$/.cat4/g:S/.5$/.cat5/g:S/.6$/.cat6/g:S/.7$/.cat7/g:S/.8$/.cat8/g:S/.9$/.cat9/g}
.if ${MCATEXT} == ""
_minstpage=`echo $$page | sed 's/\.cat/./'`
.else
_minstpage=`echo $$page | sed 's/\.cat.*//'`${MCATEXT}
.endif
.endif
.if target(${MAN:[1]})
_mfromdir?=.
.endif
_mfromdir?=${.CURDIR}
MANALL?= ${MAN}
_minstpage?=$${page}
.endif

.if !empty(MANOWN)
MAN_INSTALL_OWN ?= -o ${MANOWN} -g ${MANGRP}
MAN_CHOWN ?= chown
.else
MAN_CHOWN = :
.endif

MINSTALL=	${INSTALL} ${COPY} ${MAN_INSTALL_OWN} -m ${MANMODE}
.if defined(MANZ)
# chown and chmod are done afterward automatically
MCOMPRESS=	gzip -cf
MCOMPRESSSUFFIX= .gz
.endif

maninstall:
.if defined(MANALL) && !empty(MANALL)
	@for page in ${MANALL:T}; do \
		test -s ${_mfromdir}/$$page || continue; \
		dir=${_mandir}; \
		test -d $$dir || ${INSTALL} -d ${MAN_INSTALL_OWN} -m 775 $$dir; \
		instpage=$${dir}${MANSUBDIR}/${_minstpage}${MCOMPRESSSUFFIX}; \
		if [ X"${MCOMPRESS}" = X ]; then \
			echo ${MINSTALL} ${_mfromdir}/$$page $$instpage; \
			${MINSTALL} ${_mfromdir}/$$page $$instpage; \
		else \
			rm -f $$instpage; \
			echo ${MCOMPRESS} ${_mfromdir}/$$page \> $$instpage; \
			${MCOMPRESS} ${_mfromdir}/$$page > $$instpage; \
			${MAN_CHOWN} ${MANOWN}:${MANGRP} $$instpage; \
			chmod ${MANMODE} $$instpage; \
		fi \
	done
.if defined(MLINKS) && !empty(MLINKS)
	@set ${MLINKS}; \
	while test $$# -ge 2; do \
		page=$$1; \
		shift; \
		dir=${_mandir}; \
		l=${_minstpage}${MCOMPRESSSUFFIX}; \
		page=$$1; \
		shift; \
		dir=${_mandir}; \
		t=$${dir}${MANSUBDIR}/${_minstpage}${MCOMPRESSSUFFIX}; \
		echo $$t -\> $$l; \
		rm -f $$t; \
		ln -s $$l $$t; \
	done
.endif
.endif

.if defined(MANALL) && !empty(MANALL)
manall: ${MANALL}
all: manall
.endif

.if defined(CLEANMAN) && !empty(CLEANMAN)
cleandir: cleanman
cleanman:
	rm -f ${CLEANMAN}
.endif
.endif
