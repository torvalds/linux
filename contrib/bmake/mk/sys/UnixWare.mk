#	$Id: UnixWare.mk,v 1.3 2017/05/05 18:02:17 sjg Exp $
#	based on "Id: SunOS.5.sys.mk,v 1.6 2003/09/30 16:42:23 sjg Exp "
#	$NetBSD: sys.mk,v 1.19.2.1 1994/07/26 19:58:31 cgd Exp $
#	@(#)sys.mk	5.11 (Berkeley) 3/13/91

OS?=		UnixWare
unix?=		We run ${OS}.
ROOT_GROUP=	root

# can't fine one anywhere, so just stop the dependency
LIBCRT0= /dev/null

PATH=/usr/sbin:/usr/bin:/usr/ccs/bin:/usr/ccs/lib:/usr/ucb:/usr/local/bin

.SUFFIXES: .out .a .ln .o .c ${CXX_SUFFIXES} .F .f .r .y .l .s .S .cl .p .h .sh .m4

.LIBS:		.a

# no -X
LD_X=
LD_x=
AR?=		ar
ARFLAGS?=	rl
RANLIB?=	:

AS?=		as
AS_STDIN?=	-
AFLAGS?=
COMPILE.s?=	${AS} ${AFLAGS}
LINK.s?=	${CC} ${AFLAGS} ${LDFLAGS}
COMPILE.S?=	${CC} ${AFLAGS} ${CPPFLAGS} -c
LINK.S?=	${CC} ${AFLAGS} ${CPPFLAGS} ${LDFLAGS}

# at least gcc 2.95 on UnixWare has no internal macro to identify the system
.if exists(/usr/local/bin/gcc)
CC?=		gcc -pipe -DUNIXWARE
DBG?=		-O -g
STATIC?=	-static
.else
CC?=		cc
DBG?=		-g
STATIC?=	-Bstatic # XXX: don't know about UDK compilers
.endif
CFLAGS?=	${DBG}
COMPILE.c?=	${CC} ${CFLAGS} ${CPPFLAGS} -c
LINK.c?=	${CC} ${CFLAGS} ${CPPFLAGS} ${LDFLAGS}

.if exists(/usr/local/bin/g++)
CXX?=		g++ -DUNIXWARE
.else
CXX?=		c++ # XXX: don't know about UDK compilers
.endif
CXXFLAGS?=	${CFLAGS}
COMPILE.cc?=	${CXX} ${CXXFLAGS} ${CPPFLAGS} -c
LINK.cc?=	${CXX} ${CXXFLAGS} ${CPPFLAGS} ${LDFLAGS}

CPP?=		/usr/ccs/lib/cpp
.if defined(DESTDIR)
CPPFLAGS+=	-nostdinc -idirafter ${DESTDIR}/usr/include
.endif

MK_DEP?=	mkdeps.sh -N
.if exists(/usr/local/bin/g77)
FC?=		g77
.else
FC?=		f77 # XXX: don't know about UDK compilers
.endif
FFLAGS?=	-O
RFLAGS?=
COMPILE.f?=	${FC} ${FFLAGS} -c
LINK.f?=	${FC} ${FFLAGS} ${LDFLAGS}
COMPILE.F?=	${FC} ${FFLAGS} ${CPPFLAGS} -c
LINK.F?=	${FC} ${FFLAGS} ${CPPFLAGS} ${LDFLAGS}
COMPILE.r?=	${FC} ${FFLAGS} ${RFLAGS} -c
LINK.r?=	${FC} ${FFLAGS} ${RFLAGS} ${LDFLAGS}

INSTALL?=	/usr/ucb/install # BSD install

LEX?=		lex
LFLAGS?=
LEX.l?=		${LEX} ${LFLAGS}

LD?=		ld
LDFLAGS?=

LIBC?=		${DESTDIR}/usr/ccs/lib/libc.a
LIBCOMPAT?=
LIBCRYPT?=	${DESTDIR}/usr/lib/libcrypt.a
LIBCURSES?=	${DESTDIR}/usr/ccs/lib/libcurses.a
LIBDBM?=
LIBDES?=
LIBEDIT?=
LIBGCC?=
LIBKDB?=
LIBKRB?=
LIBKVM?=
LIBL?=		${DESTDIR}/usr/ccs/lib/libl.a
LIBM?=		${DESTDIR}/usr/ccs/lib/libm.a
LIBMP?=
LIBPC?=
LIBPCAP?=
LIBPLOT?=
LIBRESOLV?=
LIBRPCSVC?=	${DESTDIR}/usr/lib/librpcsvc.a
LIBSKEY?=	${DESTDIR}/usr/lib/libskey.a
LIBTERMCAP?=	${DESTDIR}/usr/ccs/lib/libtermcap.a
LIBUTIL?=
LIBWRAP?=
LIBY?=		${DESTDIR}/usr/ccs/lib/liby.a
LIBZ?=

LINT?=		lint
LINTFLAGS?=	-pF

LORDER?=	lorder

NM?=		nm

PC?=		pc # XXX: UDK probably does not have pc
PFLAGS?=
COMPILE.p?=	${PC} ${PFLAGS} ${CPPFLAGS} -c
LINK.p?=	${PC} ${PFLAGS} ${CPPFLAGS} ${LDFLAGS}

SHELL?=		sh

SIZE?=		size

TSORT?=		tsort

.if exists(/usr/local/bin/bison)
YACC?=		bison -y
.else
YACC?=		yacc
.endif
YFLAGS?=	-d
YACC.y?=	${YACC} ${YFLAGS}

# C
.c:
	${LINK.c} -o ${.TARGET} ${.IMPSRC} ${LDLIBS}
.c.o:
	${COMPILE.c} ${.IMPSRC}
.c.a:
	${COMPILE.c} ${.IMPSRC}
	${AR} ${ARFLAGS} $@ $*.o
	rm -f $*.o

# C++
${CXX_SUFFIXES}:
	${LINK.cc} -o ${.TARGET} ${.IMPSRC} ${LDLIBS}
${CXX_SUFFIXES:%=%.o}:
	${COMPILE.cc} ${.IMPSRC}
${CXX_SUFFIXES:%=%.a}:
	${COMPILE.cc} ${.IMPSRC}
	${AR} ${ARFLAGS} $@ $*.o
	rm -f $*.o

# Fortran/Ratfor
.f:
	${LINK.f} -o ${.TARGET} ${.IMPSRC} ${LDLIBS}
.f.o:
	${COMPILE.f} ${.IMPSRC}
.f.a:
	${COMPILE.f} ${.IMPSRC}
	${AR} ${ARFLAGS} $@ $*.o
	rm -f $*.o

.F:
	${LINK.F} -o ${.TARGET} ${.IMPSRC} ${LDLIBS}
.F.o:
	${COMPILE.F} ${.IMPSRC}
.F.a:
	${COMPILE.F} ${.IMPSRC}
	${AR} ${ARFLAGS} $@ $*.o
	rm -f $*.o

.r:
	${LINK.r} -o ${.TARGET} ${.IMPSRC} ${LDLIBS}
.r.o:
	${COMPILE.r} ${.IMPSRC}
.r.a:
	${COMPILE.r} ${.IMPSRC}
	${AR} ${ARFLAGS} $@ $*.o
	rm -f $*.o

# Pascal
.p:
	${LINK.p} -o ${.TARGET} ${.IMPSRC} ${LDLIBS}
.p.o:
	${COMPILE.p} ${.IMPSRC}
.p.a:
	${COMPILE.p} ${.IMPSRC}
	${AR} ${ARFLAGS} $@ $*.o
	rm -f $*.o

# Assembly
.s:
	${LINK.s} -o ${.TARGET} ${.IMPSRC} ${LDLIBS}
.s.o:
	${COMPILE.s} ${.IMPSRC}
.s.a:
	${COMPILE.s} ${.IMPSRC}
	${AR} ${ARFLAGS} $@ $*.o
	rm -f $*.o
.S:
	${LINK.S} -o ${.TARGET} ${.IMPSRC} ${LDLIBS}
.S.o:
	${COMPILE.S} ${.IMPSRC}
.S.a:
	${COMPILE.S} ${.IMPSRC}
	${AR} ${ARFLAGS} $@ $*.o
	rm -f $*.o

# Lex
.l:
	${LEX.l} ${.IMPSRC}
	${LINK.c} -o ${.TARGET} lex.yy.c ${LDLIBS} -ll
	rm -f lex.yy.c
.l.c:
	${LEX.l} ${.IMPSRC}
	mv lex.yy.c ${.TARGET}
.l.o:
	${LEX.l} ${.IMPSRC}
	${COMPILE.c} -o ${.TARGET} lex.yy.c 
	rm -f lex.yy.c

# Yacc
.y:
	${YACC.y} ${.IMPSRC}
	${LINK.c} -o ${.TARGET} y.tab.c ${LDLIBS}
	rm -f y.tab.c
.y.c:
	${YACC.y} ${.IMPSRC}
	mv y.tab.c ${.TARGET}
.y.o:
	${YACC.y} ${.IMPSRC}
	${COMPILE.c} -o ${.TARGET} y.tab.c
	rm -f y.tab.c

# Shell
.sh:
	rm -f ${.TARGET}
	cp ${.IMPSRC} ${.TARGET}
