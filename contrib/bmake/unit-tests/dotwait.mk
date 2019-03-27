# $NetBSD: dotwait.mk,v 1.2 2017/10/08 20:44:19 sjg Exp $

THISMAKEFILE:= ${.PARSEDIR}/${.PARSEFILE}

TESTS= simple recursive shared cycle
PAUSE= sleep 1

# Use a .for loop rather than dependencies here, to ensure
# that the tests are run one by one, with parallelism
# only within tests.
# Ignore "--- target ---" lines printed by parallel make.
all:
.for t in ${TESTS}
	@${.MAKE} -f ${THISMAKEFILE} -j4 $t 2>&1 | grep -v "^--- "
.endfor

#
# Within each test, the names of the sub-targets follow these
# conventions:
# * If it's expected that two or more targets may be made in parallel,
#   then the target names will differ only in an alphabetic component
#   such as ".a" or ".b".
# * If it's expected that two or more targets should be made in sequence
#   then the target names will differ in numeric components, such that
#   lexical ordering of the target names matches the expected order
#   in which the targets should be made.
#
# Targets may echo ${PARALLEL_TARG} to print a modified version
# of their own name, in which alphabetic components like ".a" or ".b"
# are converted to ".*".  Two targets that are expected to
# be made in parallel will thus print the same strings, so that the
# output is independent of the order in which these targets are made.
#
PARALLEL_TARG= ${.TARGET:C/\.[a-z]/.*/g:Q}
.DEFAULT:
	@echo ${PARALLEL_TARG}; ${PAUSE}; echo ${PARALLEL_TARG}
_ECHOUSE: .USE
	@echo ${PARALLEL_TARG}; ${PAUSE}; echo ${PARALLEL_TARG}

# simple: no recursion, no cycles
simple: simple.1 .WAIT simple.2

# recursive: all children of the left hand side of the .WAIT
# must be made before any child of the right hand side.
recursive: recursive.1.99 .WAIT recursive.2.99
recursive.1.99: recursive.1.1.a recursive.1.1.b _ECHOUSE
recursive.2.99: recursive.2.1.a recursive.2.1.b _ECHOUSE

# shared: both shared.1.99 and shared.2.99 depend on shared.0.
# shared.0 must be made first, even though it is a child of
# the right hand side of the .WAIT.
shared: shared.1.99 .WAIT shared.2.99
shared.1.99: shared.0 _ECHOUSE
shared.2.99: shared.2.1 shared.0 _ECHOUSE

# cycle: the cyclic dependency must not cause infinite recursion
# leading to stack overflow and a crash.
cycle: cycle.1.99 .WAIT cycle.2.99
cycle.2.99: cycle.2.98 _ECHOUSE
cycle.2.98: cycle.2.97 _ECHOUSE
cycle.2.97: cycle.2.99 _ECHOUSE
