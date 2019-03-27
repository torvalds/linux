# $NetBSD: suffixes.mk,v 1.3 2014/08/30 22:21:08 sjg Exp $

# Issues from PR 49086

# Issue 3: single suffix rules remain active after .SUFFIXES is cleared
#
# There's a rule for issue3.a, but .a is no longer a known suffix when
# targets are being made, so issue3 should not get made.
all: issue3

# Issue 4: suffix rules do not become regular rules when .SUFFIXES is cleared
#
# When the rules were encountered, .a and .b were known suffices, but later
# on they were forgotten.  These should get created as regular targets.
all: .a .a.b .b.a

# Issue 5: adding more suffixes does not make existing rules into suffix rules
#
# When the targets .c.d, .d.c, .d, .d.e, and .e.d were encountered, only .a,
# .b and .c were known suffixes, so all of them were regular rules.  Later
# rest of the suffixes were made known, so they should all be suffix
# transformation rules.
all: issue5a.d issue5b.c issue5c issue5d.e issue5e.d

# Issue 6: transformation search can end up in an infinite loop
#
# There is no file or target from which issue6.f could be made from so
# this should fail.  The bug was that because rules .e.f, .d.e and .e.d
# exist, make would try to make .f from .e and then infinitely try
# to do .e from .d and vice versa.
all: issue6.f

# Issue 10: explicit dependencies affect transformation rule selection
#
# If issue10.e is wanted and both issue10.d and issue10.f are available,
# make should choose the .d.e rule, because .d is before .f in .SUFFIXES.
# The bug was that if issue10.d had an explicit dependency on issue10.f,
# it would choose .f.e instead.
all: issue10.e

# Issue 11: sources from transformation rules are expanded incorrectly
#
# issue11.j should depend on issue11.i and issue11.second and issue11.i
# should depend on issue11.h and issue11.first.  The bug was that
# the dynamic sources were expanded before ${.PREFIX} and ${.TARGET} were
# available, so they would have expanded to a null string.
all: issue11.j

# we need to clean for repeatable results
.BEGIN: clean
clean:
	@rm -f issue* .[ab]*

.SUFFIXES: .a .b .c

.a .a.b .b.a:
	@echo 'There should be no text after the colon: ${.IMPSRC}'
	touch ${.TARGET}

.c.d .d.c .d .d.e .e.d:
	@echo 'first set'
	cp ${.IMPSRC} ${.TARGET}

.SUFFIXES:
.SUFFIXES: .c .d .e .f .g

.e .e.f .f.e:
	@echo 'second set'
	cp ${.IMPSRC} ${.TARGET}

issue3.a:
	@echo 'There is a bug if you see this.'
	touch ${.TARGET}

issue5a.c issue5b.d issue5c.d issue5d.d issue5e.e issue10.d issue10.f:
	touch ${.TARGET}

.SUFFIXES: .h .i .j

.h.i: ${.PREFIX}.first
	@echo '.ALLSRC: ${.ALLSRC}'
	cp ${.IMPSRC} ${.TARGET}

.i.j: ${.PREFIX}.second
	@echo '.ALLSRC: ${.ALLSRC}'
	cp ${.IMPSRC} ${.TARGET}

issue11.h issue11.first issue11.second:
	touch ${.TARGET}
