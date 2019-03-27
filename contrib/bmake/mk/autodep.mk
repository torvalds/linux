#
# RCSid:
#	$Id: autodep.mk,v 1.36 2016/04/05 15:58:37 sjg Exp $
#
#	@(#) Copyright (c) 1999-2010, Simon J. Gerraty
#
#	This file is provided in the hope that it will
#	be of use.  There is absolutely NO WARRANTY.
#	Permission to copy, redistribute or otherwise
#	use this file is hereby granted provided that 
#	the above copyright notice and this notice are
#	left intact. 
#      
#	Please send copies of changes and bug-fixes to:
#	sjg@crufty.net

# This module provides automagic dependency generation along the
# lines suggested in the GNU make.info
# The depend target is mainly for backwards compatibility,
# dependencies are normally updated as part of compilation.

.if !target(__${.PARSEFILE}__)
__${.PARSEFILE}__:

DEPENDFILE?= .depend
.for d in ${DEPENDFILE:N.depend}
# bmake only groks .depend
.if ${MAKE_VERSION} < 20160218
.-include <$d>
.else
.dinclude <$d>
.endif
.endfor

# it does nothing if SRCS is not defined or is empty
.if defined(SRCS) && !empty(SRCS)
DEPSRCS?=${SRCS}
__depsrcs=${DEPSRCS:M*.c}
__depsrcs+=${DEPSRCS:M*.y}
__depsrcs+=${DEPSRCS:M*.l}
__depsrcs+=${DEPSRCS:M*.s}
__depsrcs+=${DEPSRCS:M*.S}
__depsrcs+=${DEPSRCS:M*.cc}
__depsrcs+=${DEPSRCS:M*.cpp}
__depsrcs+=${DEPSRCS:M*.C}
__depsrcs+=${DEPSRCS:M*.cxx}
__depsrcs+=${DEPSRCS:M*.pc}

.for s in ${__depsrcs}
${s:T:R}.d:	$s
.endfor

__depsrcs:=${__depsrcs:T:R:S/$/.d/g}
# we also need to handle makefiles where the .d's from __depsrcs 
# don't  match those from OBJS
# we avoid using := here, since the modifier applied to OBJS
# can cause trouble if there are any undefined vars in OBJS.
__dependsrcsx?= ${__depsrcs} ${OBJS:S/.o/.d/}
__dependsrcs= ${__dependsrcsx:O:u}

# clean up any .c files we may have generated
#__gensrcs:= ${DEPSRCS:M*.y} ${DEPSRCS:M*.l}
#CLEANFILES+= ${__gensrcs:T:R:S/$/.c/g}

# set this to -MMD to ignore /usr/include
# actually it ignores <> so may not be a great idea
CFLAGS_MD?=-MD 
# -MF etc not available on all gcc versions.
# we "fix" the .o later
CFLAGS_MF?=-MF ${.TARGET:T:R}.d -MT ${.TARGET:T:R}.o
CFLAGS+= ${CFLAGS_MD} ${CFLAGS_MF}
RM?= rm

# watch out for people who don't use CPPFLAGS
CPPFLAGS_MD=${CFLAGS:M-[IUD]*} ${CPPFLAGS} 
CXXFLAGS_MD=${CXXFLAGS:M-[IUD]*} ${CPPFLAGS} 

# just in case these need to be different
CC_MD?=${CC}
CXX_MD?=${CXX}

# should have been set by sys.mk
CXX_SUFFIXES?= .cc .cpp .cxx .C

# so we can do an explicit make depend, but not otherwise
.if make(depend)
.SUFFIXES:	.d

.if empty(CFLAGS_MD)
.y.d:
	@echo updating dependencies for $<
	@${YACC} ${YFLAGS} $<
	@${SHELL} -ec "${CC_MD} -M ${CPPFLAGS_MD} y.tab.c | sed '/:/s/^/$@ /' > $@" || { ${RM} -f y.tab.c $@; false; }
	@${RM} -f y.tab.c

.l.d:
	@echo updating dependencies for $<
	${LEX} ${LFLAGS} $<
	@${SHELL} -ec "${CC_MD} -M ${CPPFLAGS_MD} lex.yy.c | sed '/:/s/^/$@ /' > $@" || { ${RM} -f lex.yy.c $@; false; }
	@${RM} -f lex.yy.c

.c.d:
	@echo updating dependencies for $<
	@${SHELL} -ec "${CC_MD} -M ${CPPFLAGS_MD} $< | sed '/:/s/^/$@ /' > $@" || { ${RM} -f $@; false; }

.s.d .S.d:
	@echo updating dependencies for $<
	@${SHELL} -ec "${CC_MD} -M ${CPPFLAGS_MD} ${AINC} $< | sed '/:/s/^/$@ /' > $@" || { ${RM} -f $@; false; }

${CXX_SUFFIXES:%=%.d}:
	@echo updating dependencies for $<
	@${SHELL} -ec "${CXX_MD} -M ${CXXFLAGS_MD} $< | sed '/:/s/^/$@ /' > $@" || { ${RM} -f $@; false; }
.else
.y.d:
	${YACC} ${YFLAGS} $<
	${CC_MD} ${CFLAGS_MD:S/D//} ${CPPFLAGS_MD} y.tab.c > $@ || { ${RM} -f y.tab.c $@; false; }
	${RM} -f y.tab.c

.l.d:
	${LEX} ${LFLAGS} $<
	${CC_MD} ${CFLAGS_MD:S/D//} ${CPPFLAGS_MD} lex.yy.c > $@ || { ${RM} -f lex.yy.c $@; false; }
	${RM} -f lex.yy.c

.c.d:
	${CC_MD} ${CFLAGS_MD:S/D//} ${CPPFLAGS_MD} $< > $@ || { ${RM} -f $@; false; }

.s.d .S.d:
	${CC_MD} ${CFLAGS_MD:S/D//} ${CPPFLAGS_MD} ${AINC} $< > $@ || { ${RM} -f $@; false; }

${CXX_SUFFIXES:%=%.d}:
	${CXX_MD} ${CFLAGS_MD:S/D//} ${CXXFLAGS_MD} $< > $@ || { ${RM} -f $@; false; }
.endif

.if !target(depend)
depend: beforedepend ${DEPENDFILE} afterdepend _SUBDIRUSE

${DEPENDFILE}:	${DEPSRCS} ${__dependsrcs}
.NOPATH:	${__dependsrcs}
.OPTIONAL:	${__dependsrcs}
.endif
.endif				# make(depend)

.if empty(CFLAGS_MD)
# make sure the .d's are generated/updated
${PROG} ${_LIBS}:	${DEPENDFILE}
.endif

.ORDER:	beforedepend ${DEPENDFILE} afterdepend

.if ${.OBJDIR} != ${.CURDIR}
__depfiles= *.d
.else
__depfiles= ${__dependsrcs}
.endif

DEPCLEANFILES= ${DEPENDFILE} ${__depfiles} y.tab.d *.tmp.d

cleandir: cleanautodepend
cleanautodepend:
	${RM} -f ${DEPCLEANFILES}

CLEANFILES+= ${DEPCLEANFILES}

.if defined(__dependsrcs) && !empty(__dependsrcs)
.if make(depend) || !(make(clean*) || make(destroy*) || make(obj) || make(*install) || make(install-*))
# this ensures we do the right thing if only building a shared or
# profiled lib
OBJ_EXTENSIONS?=.o .po .so .So
MDLIB_SED= -e '/:/s,^\([^\.:]*\)\.[psS]*o,${OBJ_EXTENSIONS:S,^,\1,},'
.ifdef NOMD_SED
.ifdef LIB
MD_SED=sed ${MDLIB_SED}
.else
MD_SED=cat
.endif
.else
# arrange to put some variable names into ${DEPENDFILE}
.ifdef LIB
MD_SED=sed ${MDLIB_SED}
.else
MD_SED=sed
.endif
SUBST_DEPVARS+= SB TOP BACKING SRC SRCDIR BASE BASEDIR
.for v in ${SUBST_DEPVARS}
.if defined(${v}) && !empty(${v})
MD_SED+= -e 's,${$v},$${$v},'
.endif
.endfor
.endif
.if (${MD_SED} == "sed")
MD_SED=cat
.endif

# this will be done whenever make finishes successfully
.if ${MAKE_VERSION:U0:[1]:C/.*-//} < 20050530
.END:
.else
.END:	${DEPENDFILE}
# we do not want to trigger building .d's just use them if they exist
${DEPENDFILE}:	${__dependsrcs:@d@${exists($d):?$d:}@}
.endif
	-@${MD_SED} ${__depfiles} > ${DEPENDFILE}.new 2> /dev/null && \
	test -s ${DEPENDFILE}.new && mv ${DEPENDFILE}.new ${DEPENDFILE}; \
	${RM} -f ${DEPENDFILE}.new
.endif
.endif
.else
depend: beforedepend afterdepend _SUBDIRUSE
.endif

.if !target(beforedepend)
beforedepend:
.endif
.if !target(afterdepend)
afterdepend:
.endif

.endif
