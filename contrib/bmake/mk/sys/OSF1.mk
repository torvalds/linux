#	$Id: OSF1.mk,v 1.8 2017/05/05 18:02:16 sjg Exp $
#	$NetBSD: sys.mk,v 1.19.2.1 1994/07/26 19:58:31 cgd Exp $
#	@(#)sys.mk	5.11 (Berkeley) 3/13/91

OS?=		OSF1
unix?=		We run ${OS}.
ROOT_GROUP=	system

# can't fine one anywhere, so just stop the dependency
LIBCRT0= /dev/null

PATH=/usr/sbin:/usr/bin:/usr/ucb:/opt/gnu/bin:/usr/ccs/bin

.SUFFIXES: .out .a .ln .o .c ${CXX_SUFFIXES} .F .f .r .y .l .s .S .cl .p .h .sh .m4

.LIBS:		.a

# no -X
LD_X=
LD_x=		-x
LD_r=		-r
AR=		ar
ARFLAGS=	rl
RANLIB=		ranlib

AS=		as
AS_STDIN=	-
AFLAGS=
COMPILE.s=	${AS} ${AFLAGS}
LINK.s=		${CC} ${AFLAGS} ${LDFLAGS}
COMPILE.S=	${CC} ${AFLAGS} ${CPPFLAGS} -c
LINK.S=		${CC} ${AFLAGS} ${CPPFLAGS} ${LDFLAGS}
.if exists(/opt/gnu/bin/gcc) || exists(/usr/local/bin/gcc)
CC?=		gcc 
.else
CC?=             cc -std
.endif
.if (${CC:T} == "gcc")
DBG=		-O -g
STATIC=		-static
DBG=         -g
STATIC=         -non_shared
.endif

CFLAGS=		${DBG}
COMPILE.c=	${CC} ${CFLAGS} ${CPPFLAGS} -c
LINK.c=		${CC} ${CFLAGS} ${CPPFLAGS} ${LDFLAGS}

CXX=		g++
CXXFLAGS=	${CFLAGS}
COMPILE.cc=	${CXX} ${CXXFLAGS} ${CPPFLAGS} -c
LINK.cc=	${CXX} ${CXXFLAGS} ${CPPFLAGS} ${LDFLAGS}

CPP=		/usr/ccs/lib/cpp
.if defined(DESTDIR)
CPPFLAGS+=	-nostdinc -idirafter ${DESTDIR}/usr/include
.endif

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

SHELL=		sh

.if exists(/usr/local/bin/bison) || exists(/opt/gnu/bin/bison)
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
