# $NetBSD: impsrc.mk,v 1.2 2014/08/30 22:21:07 sjg Exp $

# Does ${.IMPSRC} work properly?
# It should be set, in order of precedence, to ${.TARGET} of:
#  1) the implied source of a transformation rule,
#  2) the first prerequisite from the dependency line of an explicit rule, or
#  3) the first prerequisite of an explicit rule.
#

all: target1.z target2 target3 target4

.SUFFIXES: .x .y .z

.x.y: source1
	@echo 'expected: target1.x'
	@echo 'actual:   $<'

.y.z: source2
	@echo 'expected: target1.y'
	@echo 'actual:   $<'

target1.y: source3

target1.x: source4
	@echo 'expected: source4'
	@echo 'actual:   $<'

target2: source1 source2
	@echo 'expected: source1'
	@echo 'actual:   $<'

target3: source1
target3: source2 source3
	@echo 'expected: source2'
	@echo 'actual:   $<'

target4: source1
target4:
	@echo 'expected: source1'
	@echo 'actual:   $<'

source1 source2 source3 source4:

