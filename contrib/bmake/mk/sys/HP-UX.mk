#	$Id: HP-UX.mk,v 1.11 2017/05/05 18:02:16 sjg Exp $
#	$NetBSD: sys.mk,v 1.19.2.1 1994/07/26 19:58:31 cgd Exp $
#	@(#)sys.mk	5.11 (Berkeley) 3/13/91

OS=		HP-UX
ROOT_GROUP=	root
unix?=		We run ${OS}.

# HP-UX's cc does not provide any clues as to wether this is 9.x or 10.x
# nor does sys/param.h, so we'll use the existence of /hp-ux
.if exists("/hp-ux") 
OSMAJOR?=9
.endif
OSMAJOR?=10
__HPUX_VERSION?=${OSMAJOR}

.SUFFIXES: .out .a .ln .o .c ${CXX_SUFFIXES} .F .f .r .y .l .s .S .cl .p .h .sh .m4

LIBMODE= 755
LIBCRT0=	/lib/crt0.o

.LIBS:		.a

# +b<path> is needed to stop the binaries from insisting on having
#	the build tree available :-)
# +s	tells the dynamic loader to use SHLIB_PATH if set
LD_bpath?=-Wl,+b/lib:/usr/lib:/usr/local/lib
LD_spath?=-Wl,+s
LDADD+= ${LD_bpath} ${LD_spath}

.if exists(/usr/lib/end.o)
LDADD+= /usr/lib/end.o
.endif

AR=		ar
ARFLAGS=	rl
RANLIB=		:

AFLAGS=
COMPILE.s=	${AS} ${AFLAGS}
LINK.s=		${CC} ${AFLAGS} ${LDFLAGS}
COMPILE.S=	${CC} ${AFLAGS} ${CPPFLAGS} -c
LINK.S=		${CC} ${AFLAGS} ${CPPFLAGS} ${LDFLAGS}
.if exists(/usr/local/bin/gcc)
PIPE?= -pipe
CC?=		gcc ${PIPE}
AS=		gas
DBG?=		-O -g
STATIC?=		-static
.if defined(DESTDIR)
CPPFLAGS+=	-nostdinc -idirafter ${DESTDIR}/usr/include
.endif
.else
# HP's bundled compiler knows not -g or -O
AS=		as
CC=             cc
.if exists(/opt/ansic/bin/cc)
CCMODE?=-Ae +ESlit
PICFLAG?= +z
LD_x=
DBG?=-g -O
.endif
DBG?=         
STATIC?=         -Wl,-a,archive
.endif
.if (${__HPUX_VERSION} == "10")
CCSOURCE_FLAGS?= -D_HPUX_SOURCE
.else
CCSOURCE_FLAGS?= -D_HPUX_SOURCE -D_INCLUDE_POSIX_SOURCE -D_INCLUDE_XOPEN_SOURCE -D_INCLUDE_XOPEN_SOURCE_EXTENDED
.endif
CFLAGS=		${DBG}
CFLAGS+= ${CCMODE} -D__hpux__ -D__HPUX_VERSION=${__HPUX_VERSION} ${CCSOURCE_FLAGS}
COMPILE.c=	${CC} ${CFLAGS} ${CPPFLAGS} -c
LINK.c=		${CC} ${CFLAGS} ${CPPFLAGS} ${LDFLAGS}

CXX=		g++
CXXFLAGS=	${CFLAGS}
COMPILE.cc=	${CXX} ${CXXFLAGS} ${CPPFLAGS} -c
LINK.cc=	${CXX} ${CXXFLAGS} ${CPPFLAGS} ${LDFLAGS}

CPP=		cpp

MK_DEP=	mkdeps.sh -N
FC=		f77
FFLAGS=		-O
RFLAGS=
COMPILE.f=	${FC} ${FFLAGS} -c
LINK.f=		${FC} ${FFLAGS} ${LDFLAGS}
COMPILE.F=	${FC} ${FFLAGS} ${CPPFLAGS} -c
LINK.F=		${FC} ${FFLAGS} ${CPPFLAGS} ${LDFLAGS}
COMPILE.r=	${FC} ${FFLAGS} ${RFLAGS} -c
LINK.r=		${FC} ${FFLAGS} ${RFLAGS} ${LDFLAGS}

LEX=		lex
LFLAGS=
LEX.l=		${LEX} ${LFLAGS}

LD=		ld
LDFLAGS=

LINT=		lint
LINTFLAGS=	-chapbx

PC=		pc
PFLAGS=
COMPILE.p=	${PC} ${PFLAGS} ${CPPFLAGS} -c
LINK.p=		${PC} ${PFLAGS} ${CPPFLAGS} ${LDFLAGS}

# HP's sh sucks
ENV=
SHELL=		/bin/ksh

.if exists(/usr/local/bin/bison)
YACC=		bison -y
.else
YACC=		yacc
.endif
YFLAGS=		-d
YACC.y=		${YACC} ${YFLAGS}

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
	${COMPILE.s} -o ${.TARGET} ${.IMPSRC} 
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
