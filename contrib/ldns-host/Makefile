PROG=ldns-host
SRC=ldns-host.c
MAN=ldns-host.1

LOCALBASE?=/usr/local
PREFIX?=${LOCALBASE}
MANDIR?=${PREFIX}/man

XCFLAGS=${CFLAGS} -I${LOCALBASE}/include -std=c99 -Wall -Wextra -pedantic
XLDFLAGS=${LDFLAGS} -L${LOCALBASE}/lib -lldns

${PROG}: ${SRC}
	${CC} -o $@ ${XCFLAGS} ${SRC} ${XLDFLAGS}

clean:
	rm -f ${PROG}

install: ${PROG}
	cp ${PROG} ${PREFIX}/bin/
	cp ${MAN} ${MANDIR}/man1/

deinstall:
	rm -f ${PREFIX}/bin/${PROG} ${MANDIR}/man1/${MAN}
