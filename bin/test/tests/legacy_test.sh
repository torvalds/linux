#!/bin/sh

#-
# Copyright (c) June 1996 Wolfram Schneider <wosch@FreeBSD.org>. Berlin.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.

#
# TEST.sh - check if test(1) or builtin test works
#
# $FreeBSD$

# force a specified test program, e.g. `env test=/bin/test sh regress.sh'
: ${test=test}

t ()
{
	# $1 -> exit code
	# $2 -> $test expression

	count=$((count+1))
	# check for syntax errors
	syntax="`eval $test $2 2>&1`"
	ret=$?
	if test -n "$syntax"; then
		printf "not ok %s - (syntax error)\n" "$count $2"
	elif [ "$ret" != "$1" ]; then
		printf "not ok %s - (got $ret, expected $1)\n" "$count $2"
	else
		printf "ok %s\n" "$count $2"
	fi
}

count=0
echo "1..130"

t 0 'b = b'
t 0 'b == b'
t 1 'b != b'
t 0 '\( b = b \)'
t 0 '\( b == b \)'
t 1 '! \( b = b \)'
t 1 '! \( b == b \)'
t 1 '! -f /etc/passwd'

t 0 '-h = -h'
t 0 '-o = -o'
t 1 '-f = h'
t 1 '-h = f'
t 1 '-o = f'
t 1 'f = -o'
t 0 '\( -h = -h \)'
t 1 '\( a = -h \)'
t 1 '\( -f = h \)'
t 0 '-h = -h -o a'
t 0 '\( -h = -h \) -o 1'
t 0 '-h = -h -o -h = -h'
t 0 '\( -h = -h \) -o \( -h = -h \)'
t 0 'roedelheim = roedelheim'
t 1 'potsdam = berlin-dahlem'

t 0 '-d /'
t 0 '-d / -a a != b'
t 1 '-z "-z"'
t 0 '-n -n'

t 0 '0'
t 0 '\( 0 \)'
t 0 '-E'
t 0 '-X -a -X'
t 0 '-XXX'
t 0 '\( -E \)'
t 0 'true -o X'
t 0 'true -o -X'
t 0 '\( \( \( a = a \) -o 1 \) -a 1 \) -a true'
t 1 '-h /'
t 0 '-r /'
t 1 '-w /'
t 0 '-x /bin/sh'
t 0 '-c /dev/null'
t 0 '-f /etc/passwd'
t 0 '-s /etc/passwd'

t 1 '! \( 700 -le 1000 -a -n "1" -a "20" = "20" \)'
t 0 '100 -eq 100'
t 0 '100 -lt 200'
t 1 '1000 -lt 200'
t 0 '1000 -gt 200'
t 0 '1000 -ge 200'
t 0 '1000 -ge 1000'
t 1 '2 -ne 2'
t 0 '0 -eq 0'
t 1 '-5 -eq 5'
t 0 '\( 0 -eq 0 \)'
t 1 '1 -eq 0 -o a = a -a 1 -eq 0 -o a = aa'

t 1 '"" -o ""'
t 1 '"" -a ""'
t 1 '"a" -a ""'
t 0 '"a" -a ! ""'
t 1 '""'
t 0 '! ""'

t 0 '!'
t 0 '\('
t 0 '\)'

t 1 '\( = \)'
t 0 '\( != \)'
t 0 '\( ! \)'
t 0 '\( \( \)'
t 0 '\( \) \)'
t 0 '! = !'
t 1 '! != !'
t 1 '-n = \)'
t 0 '! != \)'
t 1 '! = a'
t 0 '! != -n'
t 0 '! -c /etc/passwd'

t 1 '! = = ='
t 0 '! = = \)'
t 0 '! "" -o ""'
t 1 '! "x" -o ""'
t 1 '! "" -o "x"'
t 1 '! "x" -o "x"'
t 0 '\( -f /etc/passwd \)'
t 0 '\( ! "" \)'
t 1 '\( ! -e \)'

t 0 '0 -eq 0 -a -d /'
t 0 '-s = "" -o "" = ""'
t 0 '"" = "" -o -s = ""'
t 1 '-s = "" -o -s = ""'
t 0 '-z x -o x = "#" -o x = x'
t 1 '-z y -o y = "#" -o y = x'
t 0 '0 -ne 0 -o ! -f /'
t 0 '1 -ne 0 -o ! -f /etc/passwd'
t 1 '0 -ne 0 -o ! -f /etc/passwd'

t 0 '-n ='
t 1 '-z ='
t 1 '! ='
t 0 '-n -eq'
t 1 '-z -eq'
t 1 '! -eq'
t 0 '-n -a'
t 1 '-z -a'
t 1 '! -a'
t 0 '-n -o'
t 1 '-z -o'
t 1 '! -o'
t 1 '! -n ='
t 0 '! -z ='
t 0 '! ! ='
t 1 '! -n -eq'
t 0 '! -z -eq'
t 0 '! ! -eq'
t 1 '! -n -a'
t 0 '! -z -a'
t 0 '! ! -a'
t 1 '! -n -o'
t 0 '! -z -o'
t 0 '! ! -o'
t 0 '\( -n = \)'
t 1 '\( -z = \)'
t 1 '\( ! = \)'
t 0 '\( -n -eq \)'
t 1 '\( -z -eq \)'
t 1 '\( ! -eq \)'
t 0 '\( -n -a \)'
t 1 '\( -z -a \)'
t 1 '\( ! -a \)'
t 0 '\( -n -o \)'
t 1 '\( -z -o \)'
t 1 '\( ! -o \)'
