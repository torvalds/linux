# $Id: escape.mk,v 1.1.1.2 2014/11/06 01:40:37 sjg Exp $
#
# Test backslash escaping.

# Extracts from the POSIX 2008 specification
# <http://pubs.opengroup.org/onlinepubs/9699919799/utilities/make.html>:
#
#     Comments start with a <number-sign> ( '#' ) and continue until an
#     unescaped <newline> is reached.
#
#     When an escaped <newline> (one preceded by a <backslash>) is found
#     anywhere in the makefile except in a command line, an include
#     line, or a line immediately preceding an include line, it shall
#     be replaced, along with any leading white space on the following
#     line, with a single <space>.
#
#     When an escaped <newline> is found in a command line in a
#     makefile, the command line shall contain the <backslash>, the
#     <newline>, and the next line, except that the first character of
#     the next line shall not be included if it is a <tab>.
#
#     When an escaped <newline> is found in an include line or in a
#     line immediately preceding an include line, the behavior is
#     unspecified.
#
# Notice that the behaviour of <backslash><backslash> or
# <backslash><anything other than newline> is not mentioned.  I think
# this implies that <backslash> should be taken literally everywhere
# except before <newline>.
#
# Our practice, despite what POSIX might say, is that "\#"
# in a variable assignment stores "#" as part of the value.
# The "\" is not taken literally, and the "#" does not begin a comment.
#
# Also, our practice is that an even number of backslashes before a
# newline in a variable assignment simply stores the backslashes as part
# of the value, and treats the newline as though it was not escaped.
# Similarly, ann even number of backslashes before a newline in a
# command simply uses the backslashes as part of the command test, but
# does not escape the newline.  This is compatible with GNU make.

all: .PHONY
# We will add dependencies like "all: yet-another-test" later.

# Some variables to be expanded in tests
#
a = aaa
A = ${a}

# Backslash at end of line in a comment\
should continue the comment. \
# This is also tested in comment.mk.

__printvars: .USE .MADE
	@echo ${.TARGET}
	${.ALLSRC:@v@ printf "%s=:%s:\n" ${v:Q} ${${v}:Q}; @}

# Embedded backslash in variable should be taken literally.
#
VAR1BS = 111\111
VAR1BSa = 111\${a}
VAR1BSA = 111\${A}
VAR1BSda = 111\$${a}
VAR1BSdA = 111\$${A}
VAR1BSc = 111\# backslash escapes comment char, so this is part of the value
VAR1BSsc = 111\ # This is a comment.  Value ends with <backslash><space>

all: var-1bs
var-1bs: .PHONY __printvars VAR1BS VAR1BSa VAR1BSA VAR1BSda VAR1BSdA \
	VAR1BSc VAR1BSsc

# Double backslash in variable should be taken as two literal backslashes.
#
VAR2BS = 222\\222
VAR2BSa = 222\\${a}
VAR2BSA = 222\\${A}
VAR2BSda = 222\\$${a}
VAR2BSdA = 222\\$${A}
VAR2BSc = 222\\# backslash does not escape comment char, so this is a comment
VAR2BSsc = 222\\ # This is a comment.  Value ends with <backslash><backslash>

all: var-2bs
var-2bs: .PHONY __printvars VAR2BS VAR2BSa VAR2BSA VAR2BSda VAR2BSdA \
	VAR2BSc VAR2BSsc

# Backslash-newline in a variable setting is replaced by a single space.
#
VAR1BSNL = 111\
111
VAR1BSNLa = 111\
${a}
VAR1BSNLA = 111\
${A}
VAR1BSNLda = 111\
$${a}
VAR1BSNLdA = 111\
$${A}
VAR1BSNLc = 111\
# this should be processed as a comment
VAR1BSNLsc = 111\
 # this should be processed as a comment

all: var-1bsnl
var-1bsnl:	.PHONY
var-1bsnl: .PHONY __printvars \
	VAR1BSNL VAR1BSNLa VAR1BSNLA VAR1BSNLda VAR1BSNLdA \
	VAR1BSNLc VAR1BSNLsc

# Double-backslash-newline in a variable setting.
# Both backslashes should be taken literally, and the newline is NOT escaped.
#
# The second lines below each end with '=' so that they will not
# generate syntax errors regardless of whether or not they are
# treated as part of the value.
#
VAR2BSNL = 222\\
222=
VAR2BSNLa = 222\\
${a}=
VAR2BSNLA = 222\\
${A}=
VAR2BSNLda = 222\\
$${a}=
VAR2BSNLdA = 222\\
$${A}=
VAR2BSNLc = 222\\
# this should be processed as a comment
VAR2BSNLsc = 222\\
 # this should be processed as a comment

all: var-2bsnl
var-2bsnl: .PHONY __printvars \
	VAR2BSNL VAR2BSNLa VAR2BSNLA VAR2BSNLda VAR2BSNLdA \
	VAR2BSNLc VAR2BSNLsc

# Triple-backslash-newline in a variable setting.
# First two should be taken literally, and last should escape the newline.
#
# The second lines below each end with '=' so that they will not
# generate syntax errors regardless of whether or not they are
# treated as part of the value.
#
VAR3BSNL = 333\\\
333=
VAR3BSNLa = 333\\\
${a}=
VAR3BSNLA = 333\\\
${A}=
VAR3BSNLda = 333\\\
$${a}=
VAR3BSNLdA = 333\\\
$${A}=
VAR3BSNLc = 333\\\
# this should be processed as a comment
VAR3BSNLsc = 333\\\
 # this should be processed as a comment

all: var-3bsnl
var-3bsnl: .PHONY __printvars \
	VAR3BSNL VAR3BSNLa VAR3BSNLA VAR3BSNLda VAR3BSNLdA \
	VAR3BSNLc VAR3BSNLsc

# Backslash-newline in a variable setting, plus any amount of white space
# on the next line, is replaced by a single space.
#
VAR1BSNL00= first line\

# above line is entirely empty, and this is a comment
VAR1BSNL0= first line\
no space on second line
VAR1BSNLs= first line\
 one space on second line
VAR1BSNLss= first line\
  two spaces on second line
VAR1BSNLt= first line\
	one tab on second line
VAR1BSNLtt= first line\
		two tabs on second line
VAR1BSNLxx= first line\
  	 	 	 many spaces and tabs [  	 ] on second line

all: var-1bsnl-space
var-1bsnl-space: .PHONY __printvars \
	VAR1BSNL00 VAR1BSNL0 VAR1BSNLs VAR1BSNLss VAR1BSNLt VAR1BSNLtt \
	VAR1BSNLxx

# Backslash-newline in a command is retained.
#
# The "#" in "# second line without space" makes it a comment instead
# of a syntax error if the preceding line is parsed incorretly.
# The ":" in "third line':" makes it look like the start of a
# target instead of a syntax error if the first line is parsed incorrectly.
#
all: cmd-1bsnl
cmd-1bsnl: .PHONY
	@echo ${.TARGET}
	echo :'first line\
#second line without space\
third line':
	echo :'first line\
     second line spaces should be retained':
	echo :'first line\
	second line tab should be elided':
	echo :'first line\
		only one tab should be elided, second tab remains'

# When backslash-newline appears at the end of a command script,
# both the backslash and the newline should be passed to the shell.
# The shell should elide the backslash-newline.
#
all: cmd-1bsnl-eof
cmd-1bsnl-eof:
	@echo ${.TARGET}
	echo :'command ending with backslash-newline'; \

# above line must be blank

# Double-backslash-newline in a command.
# Both backslashes are retained, but the newline is not escaped.
# XXX: This may differ from POSIX, but matches gmake.
#
# When make passes two backslashes to the shell, the shell will pass one
# backslash to the echo commant.
#
all: cmd-2bsnl
cmd-2bsnl: .PHONY
	@echo ${.TARGET}
	echo take one\\
# this should be a comment
	echo take two\\
	echo take three\\

# Triple-backslash-newline in a command is retained.
#
all: cmd-3bsnl
cmd-3bsnl: .PHONY
	@echo ${.TARGET}
	echo :'first line\\\
#second line without space\\\
third line':
	echo :'first line\\\
     second line spaces should be retained':
	echo :'first line\\\
	second line tab should be elided':
	echo :'first line\\\
		only one tab should be elided, second tab remains'
