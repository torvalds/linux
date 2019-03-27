# $Id: dep.mk,v 1.17 2014/08/04 05:12:27 sjg Exp $

.if !target(__${.PARSEFILE}__)
__${.PARSEFILE}__:

# handle Proc*C as well...
.if defined(SRCS)
.if !empty(SRCS:M*.pc)
.include <proc.mk>
.endif

# it would be nice to be able to query .SUFFIXES
OBJ_EXTENSIONS+= .o .po .lo .So

# explicit dependencies help short-circuit .SUFFIX searches
SRCS_DEP_FILTER+= N*.[hly]
.for s in ${SRCS:${SRCS_DEP_FILTER:O:u:ts:}}
.for e in ${OBJ_EXTENSIONS:O:u}
.if !target(${s:T:R}$e)
${s:T:R}$e: $s
.endif
.endfor
.endfor
.endif

.if exists(/usr/bin/mkdep)
MKDEP_CMD?=	mkdep
.elif exists(/usr/local/share/bin/mkdeps.sh)
MKDEP_CMD?=	/usr/local/share/bin/mkdeps.sh -N
.endif
MKDEP_CMD?=	mkdep

MKDEP ?= ${MKDEP_CMD}

.NOPATH:	.depend

.if ${MKDEP_MK:Uno} == "auto.dep.mk" && make(depend)
# auto.dep.mk does not "do" depend
MK_AUTODEP= no
.endif

.if ${MK_AUTODEP} == yes
MKDEP_MK ?= autodep.mk
.include <${MKDEP_MK}>
.else
MKDEP_ENV_VARS += CC CXX
.for v in ${MKDEP_ENV_VARS:O:u}
.if !empty($v)
MKDEP_ENV += $v='${$v}'
.endif
.endfor

_MKDEP = ${MKDEP_ENV} ${MKDEP}

# some of the rules involve .h sources, so remove them from mkdep line
.if !target(depend)
depend: beforedepend .depend _SUBDIRUSE afterdepend

.if defined(SRCS)
# libs can have too many SRCS for a single command line
# so do them one at a time.
.depend: ${SRCS} ${.PARSEDIR}/${.PASEFILE}
	@rm -f .depend
.ifdef LIB
	@files="${.ALLSRC:M*.[sS]}"; \
	set -x; for f in $$files; do ${_MKDEP} -a ${MKDEPFLAGS} \
	    ${CFLAGS:M-[ID]*} ${CPPFLAGS} ${AINC} $$f; done
	@files="${.ALLSRC:M*.c} ${.ALLSRC:M*.pc:T:.pc=.c}"; \
	set -x; for f in $$files; do ${_MKDEP} -a ${MKDEPFLAGS} \
	    ${CFLAGS:M-[ID]*} ${CPPFLAGS} $$f; done
	@files="${.ALLSRC:M*.cc} ${.ALLSRC:M*.C} ${.ALLSRC:M*.cxx}"; \
	set -x; for f in $$files; do ${_MKDEP} -a ${MKDEPFLAGS} \
	    ${CXXFLAGS:M-[ID]*} ${CPPFLAGS} $$f; done
.else
	@files="${.ALLSRC:M*.[Ss]}"; \
	case "$$files" in *.[Ss]*) \
	  echo ${_MKDEP} -a ${MKDEPFLAGS} \
	    ${CFLAGS:M-[ID]*} ${CPPFLAGS} ${AINC} $$files; \
	  ${_MKDEP} -a ${MKDEPFLAGS} \
	    ${CFLAGS:M-[ID]*} ${CPPFLAGS} ${AINC} $$files;; \
	esac
	@files="${.ALLSRC:M*.c} ${.ALLSRC:M*.pc:T:.pc=.c}"; \
	case "$$files" in *.c*) \
	  echo ${_MKDEP} -a ${MKDEPFLAGS} \
	    ${CFLAGS:M-[ID]*} ${CPPFLAGS} $$files; \
	  ${_MKDEP} -a ${MKDEPFLAGS} \
	    ${CFLAGS:M-[ID]*} ${CPPFLAGS} $$files;; \
	esac
	@files="${.ALLSRC:M*.cc} ${.ALLSRC:M*.C} ${.ALLSRC:M*.cxx}"; \
	case "$$files" in *.[Cc]*) \
	  echo ${_MKDEP} -a ${MKDEPFLAGS} \
	    ${CXXFLAGS:M-[ID]*} ${CPPFLAGS} $$files; \
	  ${_MKDEP} -a ${MKDEPFLAGS} \
	    ${CXXFLAGS:M-[ID]*} ${CPPFLAGS} $$files;; \
	esac
.endif
.else
.depend:
.endif
.if !target(beforedepend)
beforedepend:
.endif
.if !target(afterdepend)
afterdepend:
.endif
.endif
.endif

.if !target(tags)
.if defined(SRCS)
tags: ${SRCS} _SUBDIRUSE
	-cd ${.CURDIR}; ctags -f /dev/stdout ${.ALLSRC:N*.h} | \
	    sed "s;\${.CURDIR}/;;" > tags
.else
tags:
.endif
.endif

.if defined(SRCS)
cleandir: cleandepend
.if !target(cleandepend)
cleandepend:
	rm -f .depend ${.CURDIR}/tags
.endif
.endif

.endif
