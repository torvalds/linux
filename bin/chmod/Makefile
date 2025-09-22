#	$OpenBSD: Makefile,v 1.9 2017/05/28 08:03:36 awolk Exp $

PROG=	chmod
MAN=	chmod.1 chgrp.1 chown.8 chflags.1
LINKS=	${BINDIR}/chmod ${BINDIR}/chgrp \
	${BINDIR}/chmod /sbin/chown

# XXX compatibility
afterinstall:
	(cd ${DESTDIR}/usr/sbin && \
	    ln -sf ../../sbin/chown . && \
	    ln -sf ../../bin/chgrp . && \
	    chown -h ${BINOWN}:${BINGRP} chown chgrp)
	(cd ${DESTDIR}/usr/bin && \
	    ln -sf ../../bin/chmod chflags && \
	    chown -h ${BINOWN}:${BINGRP} chflags)


.include <bsd.prog.mk>
