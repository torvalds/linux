# $Id: cond1.mk,v 1.1.1.1 2014/08/30 18:57:18 sjg Exp $

# hard code these!
TEST_UNAME_S= NetBSD
TEST_UNAME_M= sparc
TEST_MACHINE= i386

.if ${TEST_UNAME_S}
Ok=var,
.endif
.if ("${TEST_UNAME_S}")
Ok+=(\"var\"),
.endif
.if (${TEST_UNAME_M} != ${TEST_MACHINE})
Ok+=(var != var),
.endif
.if ${TEST_UNAME_M} != ${TEST_MACHINE}
Ok+= var != var,
.endif
.if !((${TEST_UNAME_M} != ${TEST_MACHINE}) && defined(X))
Ok+= !((var != var) && defined(name)),
.endif
# from bsd.obj.mk
MKOBJ?=no
.if ${MKOBJ} == "no"
o= no
Ok+= var == "quoted",
.else
.if defined(notMAKEOBJDIRPREFIX) || defined(norMAKEOBJDIR)
.if defined(notMAKEOBJDIRPREFIX)
o=${MAKEOBJDIRPREFIX}${__curdir}
.else
o= ${MAKEOBJDIR}
.endif
.endif
o= o
.endif

# repeat the above to check we get the same result
.if ${MKOBJ} == "no"
o2= no
.else
.if defined(notMAKEOBJDIRPREFIX) || defined(norMAKEOBJDIR)
.if defined(notMAKEOBJDIRPREFIX)
o2=${MAKEOBJDIRPREFIX}${__curdir}
.else
o2= ${MAKEOBJDIR}
.endif
.endif
o2= o
.endif

PRIMES=2 3 5 7 11
NUMBERS=1 2 3 4 5

n=2
.if ${PRIMES:M$n} == ""
X=not
.else
X=
.endif

.if ${MACHINE_ARCH} == no-such
A=one
.else
.if ${MACHINE_ARCH} == not-this
.if ${MACHINE_ARCH} == something-else
A=unlikely
.else
A=no
.endif
.endif
A=other
# We expect an extra else warning - we're not skipping here
.else
A=this should be an error
.endif

.if $X != ""
.if $X == not
B=one
.else
B=other
# We expect an extra else warning - we are skipping here
.else
B=this should be an error
.endif
.else
B=unknown
.endif

.if "quoted" == quoted
C=clever
.else
C=dim
.endif

.if defined(nosuch) && ${nosuch:Mx} != ""
# this should not happen
.info nosuch is x
.endif

all:
	@echo "$n is $X prime"
	@echo "A='$A' B='$B' C='$C' o='$o,${o2}'"
	@echo "Passed:${.newline} ${Ok:S/,/${.newline}/}"
	@echo "${NUMBERS:@n@$n is ${("${PRIMES:M$n}" == ""):?not:} prime${.newline}@}"
	@echo "${"${DoNotQuoteHere:U0}" > 0:?OK:No}"
	@echo "${${NoSuchNumber:U42} > 0:?OK:No}"
