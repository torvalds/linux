# This is a comment
.if ${MACHINE_ARCH} == something
FOO=bar
.endif

#\
	Multiline comment

BAR=# defined
FOOBAR= # defined 

# This is an escaped comment \
that keeps going until the end of this line

# Another escaped comment \
that \
goes \
on

# This is NOT an escaped comment due to the double backslashes \\
all: hi foo bar
	@echo comment testing done

hi:
	@echo comment testing start

foo:
	@echo this is $@

bar:
	@echo This is how a comment looks: '# comment'
