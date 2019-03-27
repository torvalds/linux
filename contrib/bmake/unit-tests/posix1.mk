# $NetBSD: posix1.mk,v 1.3 2014/08/30 22:21:08 sjg Exp $

# Keep the default suffixes from interfering, just in case.
.SUFFIXES:

all:	line-continuations suffix-substitution localvars

# we need to clean for repeatable results
.BEGIN: clean
clean:
	@rm -f lib.a dir/* dummy obj*

#
# Line continuations
#

# Escaped newlines and leading whitespace from the next line are replaced
# with single space, except in commands, where the escape and the newline
# are retained, but a single leading tab (if any) from the next line is
# removed. (PR 49085)
# Expect:
# ${VAR} = "foo  bar baz"
# a
# b
# c
VAR = foo\
\
	  bar\
 baz

line-continuations:
	@echo '$${VAR} = "${VAR}"'
	@echo 'aXbXc' | sed -e 's/X/\
	/g'


#
# Suffix substitution
#

# The only variable modifier accepted by POSIX.
# ${VAR:s1=s2}: replace s1, if found, with s2 at end of each word in
# ${VAR}.  s1 and s2 may contain macro expansions.
# Expect: foo baR baz, bar baz, foo bar baz, fooadd baradd bazadd
suffix-substitution:
	@echo '${VAR:r=R}, ${VAR:foo=}, ${VAR:not_there=wrong}, ${VAR:=add}'


#
# Local variables: regular forms, D/F forms and suffix substitution.
#

# In the past substitutions did not work with the D/F forms and those
# forms were not available for $?.  (PR 49085)

ARFLAGS = -rcv

localvars: lib.a

# $@ = target or archive name	$< = implied source
# $* = target without suffix 	$? = sources newer than target
# $% = archive member name
LOCALS = \
	"Local variables\n\
	\$${@}=\"${@}\" \$${<}=\"${<}\"\n\
	\$${*}=\"${*}\" \$${?}=\"${?}\"\n\
	\$${%%}=\"${%}\"\n\n"

# $XD = directory part of X	$XF = file part of X
# X is one of the local variables.
LOCAL_ALTERNATIVES = \
	"Directory and filename parts of local variables\n\
	\$${@D}=\"${@D}\" \$${@F}=\"${@F}\"\n\
	\$${<D}=\"${<D}\" \$${<F}=\"${<F}\"\n\
	\$${*D}=\"${*D}\" \$${*F}=\"${*F}\"\n\
	\$${?D}=\"${?D}\" \$${?F}=\"${?F}\"\n\
	\$${%%D}=\"${%D}\" \$${%%F}=\"${%F}\"\n\n"

# Do all kinds of meaningless substitutions on local variables to see
# if they work.  Add, remove and replace things.
VAR2 = .o
VAR3 = foo
LOCAL_SUBSTITUTIONS = \
	"Local variable substitutions\n\
	\$${@:.o=}=\"${@:.o=}\" \$${<:.c=.C}=\"${<:.c=.C}\"\n\
	\$${*:=.h}=\"${*:=.h}\" \$${?:.h=.H}=\"${?:.h=.H}\"\n\
	\$${%%:=}=\"${%:=}\"\n\n"

LOCAL_ALTERNATIVE_SUBSTITUTIONS = \
	"Target with suffix transformations\n\
	\$${@D:=append}=\"${@D:=append}\"\n\
	\$${@F:.o=.O}=\"${@F:.o=.O}\"\n\
	\n\
	Implied source with suffix transformations\n\
	\$${<D:r=rr}=\"${<D:r=rr}\"\n\
	\$${<F:.c=.C}=\"${<F:.c=.C}\"\n\
	\n\
	Suffixless target with suffix transformations\n\
	\$${*D:.=dot}=\"${*D:.=dot}\"\n\
	\$${*F:.a=}=\"${*F:.a=}\"\n\
	\n\
	Out-of-date dependencies with suffix transformations\n\
	\$${?D:ir=}=\"${?D:ir=}\"\n\
	\$${?F:.h=.H}=\"${?F:.h=.H}\"\n\
	\n\
	Member with suffix transformations\n\
	\$${%%D:.=}=\"${%D:.=}\"\n\
	\$${%%F:\$${VAR2}=\$${VAR}}=\"${%F:${VAR2}=${VAR}}\"\n\n"

.SUFFIXES: .c .o .a

# The system makefiles make the .c.a rule .PRECIOUS with a special source,
# but such a thing is not POSIX compatible.  It's also somewhat useless
# in a test makefile.
.c.a:
	@printf ${LOCALS}
	@printf ${LOCAL_ALTERNATIVES}
	@printf ${LOCAL_SUBSTITUTIONS}
	@printf ${LOCAL_ALTERNATIVE_SUBSTITUTIONS}
	cc -c -o '${%}' '${<}'
	ar ${ARFLAGS} '${@}' '${%}'
	rm -f '${%}'

.c.o:
	@printf ${LOCALS}
	@printf ${LOCAL_ALTERNATIVES}
	@printf ${LOCAL_SUBSTITUTIONS}
	@printf ${LOCAL_ALTERNATIVE_SUBSTITUTIONS}
	cc -c -o '${@}' '${<}'

# Some of these rules are padded with useless extra dependencies just so
# that ${?} has more than one file.

lib.a: lib.a(obj1.o) lib.a(obj2.o) lib.a(obj3.o)
	@ar -s '${@}'

# Explicit rule where the dependency is an inferred file.  The dependency
# object's name differs from the member's because there was a bug which
# forced a dependency on member even when no such dependency was specified
# (PR 49086).
lib.a(obj1.o): dir/obj_1.o dummy
	@printf ${LOCALS}
	@printf ${LOCAL_ALTERNATIVES}
	@printf ${LOCAL_SUBSTITUTIONS}
	@printf ${LOCAL_ALTERNATIVE_SUBSTITUTIONS}
	cp 'dir/obj_1.o' '$%'
	ar ${ARFLAGS} '${@}' '$%'
	rm -f '$%'

# Excplicit rule where the dependency also has an explicit rule.
lib.a(obj2.o): obj2.o
	ar ${ARFLAGS} '${@}' '${%}'

# Use .c.a inference with an extra dependency.
lib.a(obj3.o): obj3.h dir/dummy

# Use .c.o inference with an extra dependency.
dir/obj_1.o: dir/obj_1.h

# According to POSIX, $* is only required for inference rules and $<'s
# value is unspecified outside of inference rules.  Strictly speaking
# we shouldn't be expanding them here but who cares.  At least we get
# to check that the program does nothing stupid (like crash) with them.
# The C file is named differently from the object file because there
# was a bug which forced dependencies based on inference rules on all
# applicable targets (PR 49086).
obj2.o: obj_2.c obj_2.h dir/obj_1.h
	@printf ${LOCALS}
	@printf ${LOCAL_ALTERNATIVES}
	@printf ${LOCAL_SUBSTITUTIONS}
	@printf ${LOCAL_ALTERNATIVE_SUBSTITUTIONS}
	cc -c -o '${@}' 'obj_2.c'

# Hey, this is make, we can make our own test data setup!  obj1.c
# and obj2.c are not used, so they should not get created.  They're here
# as a bait for a regression into the forced dependencies discussed earlier.
obj1.c dir/obj_1.c obj2.c obj_2.c obj3.c:
	mkdir -p '${@D}'
	printf '#include "${@F:.c=.h}"\nconst char* ${@F:.c=} = "${@}";\n' \
	    >'${@}'

dir/obj_1.h obj_2.h obj3.h dummy dir/dummy:
	mkdir -p '${@D}'
	touch '${@}'
