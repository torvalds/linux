#	from: @(#)Makefile	5.11 (Berkeley) 5/21/91
# $FreeBSD$

.include <src.opts.mk>

FILESGROUPS=	FILES
NLS_ALIASES=	POSIX C \
		en_US.US_ASCII C

# No need as it is empty and just causes rebuilds since this file does so much.
UPDATE_DEPENDFILE=	no

.if ${MK_SENDMAIL} != "no"
SUBDIR+=sendmail
.endif

BIN1=	\
	group \
	login.access \
	rc.bsdextended \
	rc.firewall \
	termcap.small

# NB: keep these sorted by MK_* knobs

.if ${MK_SENDMAIL} != "no"
BIN1+=	rc.sendmail
.endif

.if ${MK_SENDMAIL} == "no"
ETCMAIL=mailer.conf aliases
.else
ETCMAIL=Makefile README mailer.conf access.sample virtusertable.sample \
	mailertable.sample aliases
.endif

# Special top level files for FreeBSD
FREEBSD=COPYRIGHT

# Sanitize DESTDIR
DESTDIR:=	${DESTDIR:C://*:/:g}

afterinstall:
.if ${MK_MAN} != "no"
	${_+_}cd ${SRCTOP}/share/man; ${MAKE} makedb
.endif

distribute:
	# Avoid installing tests here; "make distribution" will do this and
	# correctly place them in the right location.
	${_+_}cd ${.CURDIR} ; ${MAKE} MK_TESTS=no install \
	    DESTDIR=${DISTDIR}/${DISTRIBUTION}
	${_+_}cd ${.CURDIR} ; ${MAKE} distribution DESTDIR=${DISTDIR}/${DISTRIBUTION}

.include <bsd.endian.mk>

.if defined(NO_ROOT)
METALOG.add?=	cat -l >> ${METALOG}
.endif

distribution:
.if !defined(DESTDIR)
	@echo "set DESTDIR before running \"make ${.TARGET}\""
	@false
.endif
	cd ${.CURDIR}; \
	    ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 \
		${BIN1} ${DESTDIR}/etc; \
	    ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 600 \
		master.passwd ${DESTDIR}/etc;

.if ${MK_TCSH} == "no"
	sed -i "" -e 's;/bin/csh;/bin/sh;' ${DESTDIR}/etc/master.passwd
.endif
	pwd_mkdb -i -p -d ${DESTDIR}/etc ${DESTDIR}/etc/master.passwd
.if defined(NO_ROOT)
	( \
		echo "./etc/passwd type=file mode=0644 uname=root gname=wheel"; \
		echo "./etc/pwd.db type=file mode=0644 uname=root gname=wheel"; \
		echo "./etc/spwd.db type=file mode=0600 uname=root gname=wheel"; \
	) | ${METALOG.add}
.endif
	${_+_}cd ${.CURDIR}/gss; ${MAKE} install
	${_+_}cd ${.CURDIR}/mtree; ${MAKE} install
	${_+_}cd ${SRCTOP}/share/termcap; ${MAKE} etc-termcap
	${_+_}cd ${SRCTOP}/usr.sbin/rmt; ${MAKE} etc-rmt
.if ${MK_UNBOUND} != "no"
	if [ ! -e ${DESTDIR}/etc/unbound ]; then \
		${INSTALL_SYMLINK} ../var/unbound ${DESTDIR}/etc/unbound; \
	fi
.endif
.if ${MK_SENDMAIL} != "no"
	${_+_}cd ${.CURDIR}/sendmail; ${MAKE} distribution
.endif
.if ${MK_KERBEROS} != "no"
	cd ${.CURDIR}/root; \
	    ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 \
		dot.k5login ${DESTDIR}/root/.k5login;
.endif

.if ${MK_MAIL} != "no"
	cd ${.CURDIR}/mail; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 \
	    ${ETCMAIL} ${DESTDIR}/etc/mail
	if [ -d ${DESTDIR}/etc/mail -a -f ${DESTDIR}/etc/mail/aliases -a \
	      ! -f ${DESTDIR}/etc/aliases ]; then \
		${INSTALL_SYMLINK} mail/aliases ${DESTDIR}/etc/aliases; \
	fi
.endif
.if ${MK_LOCATE} != "no"
	${INSTALL} -o nobody -g ${BINGRP} -m 644 /dev/null \
	    ${DESTDIR}/var/db/locate.database
.endif
	cd ${.CURDIR}/..; ${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
		${FREEBSD} ${DESTDIR}/
.if ${MK_BOOT} != "no"
.if exists(${SRCTOP}/sys/${MACHINE}/conf/GENERIC.hints)
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${SRCTOP}/sys/${MACHINE}/conf/GENERIC.hints \
	    ${DESTDIR}/boot/device.hints
.endif
.endif

MTREE_CMD?=	mtree

MTREES=		mtree/BSD.root.dist		/		\
		mtree/BSD.var.dist		/var		\
		mtree/BSD.usr.dist		/usr		\
		mtree/BSD.include.dist		/usr/include	\
		mtree/BSD.debug.dist		/usr/lib
.if ${MK_LIB32} != "no"
MTREES+=	mtree/BSD.lib32.dist		/usr
MTREES+=	mtree/BSD.lib32.dist		/usr/lib/debug/usr
.endif
.if ${MK_LIBSOFT} != "no"
MTREES+=	mtree/BSD.libsoft.dist		/usr
MTREES+=	mtree/BSD.libsoft.dist		/usr/lib/debug/usr
.endif
.if ${MK_TESTS} != "no"
MTREES+=	mtree/BSD.tests.dist		${TESTSBASE}
MTREES+=	mtree/BSD.tests.dist		/usr/lib/debug/${TESTSBASE}
.endif
.if ${MK_SENDMAIL} != "no"
MTREES+=	mtree/BSD.sendmail.dist		/
.endif
.for mtree in ${LOCAL_MTREE}
MTREES+=	../${mtree}			/
.endfor

# Clean up some directories that where mistakenly created as files that
# should not have been as part of the nvi update in r281994.
# This should be removed after 11.0-RELEASE.
DISTRIB_CLEANUP_SHARE_FILES=	${SHAREDIR}/doc/usd/10.exref ${SHAREDIR}/doc/usd/11.edit
DISTRIB_CLEANUP_SHARE_FILES+=	${SHAREDIR}/doc/usd/12.vi ${SHAREDIR}/doc/usd/13.viref
distrib-cleanup: .PHONY
	for file in ${DISTRIB_CLEANUP_SHARE_FILES}; do \
		if [ -f ${DESTDIR}/$${file} ]; then \
			rm -f ${DESTDIR}/$${file}; \
		fi; \
	done

distrib-dirs: ${MTREES:N/*} distrib-cleanup .PHONY
.for _m _d in ${MTREES}
	@m=${.CURDIR}/${_m}; \
	d=${DESTDIR}${_d}; \
	test -d $$d || mkdir -p $$d; \
	${ECHO} ${MTREE_CMD} -deU ${MTREE_FSCHG} \
	    ${MTREE_FOLLOWS_SYMLINKS} -f $$m -p $$d; \
	${MTREE_FILTER} $$m | \
	${MTREE_CMD} -deU ${MTREE_FSCHG} ${MTREE_FOLLOWS_SYMLINKS} \
	    -p $$d
.endfor
.if defined(NO_ROOT)
.for _m _d in ${MTREES}
	@m=${.CURDIR}/${_m}; \
	d=${_d}; \
	test "$$d" == "/" && d=""; \
	d=${DISTBASE}$$d; \
	test -d ${DESTDIR}/$$d || mkdir -p ${DESTDIR}/$$d; \
	${ECHO} "${MTREE_CMD:N-W} -C -f $$m -K all | " \
	    "sed s#^\.#.$$d# | ${METALOG.add}" ; \
	${MTREE_FILTER} $$m | \
	${MTREE_CMD:N-W} -C -K all | sed s#^\.#.$$d# | \
	    ${METALOG.add}
.endfor
.endif
.if ${MK_NLS} != "no"
.for alias nls in ${NLS_ALIASES}
	${INSTALL_SYMLINK} "${nls}" "${DESTDIR}${SHAREDIR}/nls/${alias}"
.endfor
.endif

etc-examples: ${META_DEPS}
	cd ${.CURDIR}; ${INSTALL} ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${BIN1} ${BIN2} \
	    ${DESTDIR}${SHAREDIR}/examples/etc

.include <bsd.prog.mk>

.if ${MK_INSTALL_AS_USER} == "yes" && ${_uid} != 0
MTREE_FILTER= sed -e 's,\([gu]\)name=,\1id=,g' \
	-e 's,\(uid=\)[^ ]* ,\1${_uid} ,' \
	-e 's,\(gid=\)[^ ]* ,\1${_gid} ,' \
	-e 's,\(uid=\)[^ ]*$$,\1${_uid},' \
	-e 's,\(gid=\)[^ ]*$$,\1${_gid},'
.else
MTREE_FILTER= cat
.if !defined(NO_FSCHG)
MTREE_FSCHG=	-i
.endif
.endif
