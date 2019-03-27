#

CFLAGS+=	 -I${.CURDIR}/../../include
CFLAGS+=	-I${.CURDIR}/../../kernel/mysys
CFLAGS+= 	-Wall ${BPCFLAGS}

.if !exists(${SYSDIR}/netsmb/smb.h)
CFLAGS+=	-I${.CURDIR}/../../kernel
.else
CFLAGS+=	-I${SYSDIR}
.endif

.if defined(SMBGDB)
CFLAGS+=	-ggdb
.endif

CLEANFILES+=.depend

deinstall: deinstall-lib deinstall-src

