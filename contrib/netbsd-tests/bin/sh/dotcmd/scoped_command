#!/bin/sh
#
# $NetBSD: scoped_command,v 1.2 2016/03/27 14:57:50 christos Exp $
#
# Copyright (c) 2014 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Jarmo Jaakkola.
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
# THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#

: ${TEST_SH:=/bin/sh}

sane_sh()
{
	set -- ${TEST_SH}
	case "$#" in
	(0)   set /bin/sh;;
	(1|2) ;;
	(*)   set "$1";;   # Just ignore options if we cannot make them work
	esac

	case "$1" in
	/*)	TEST_SH="$1${2+ }$2";;
	./*)	TEST_SH="${PWD}${1#.}${2+ }$2";;
	*/*)	TEST_SH="${PWD}/$1${2+ }$2";;
	*)	TEST_SH="$( command -v "$1" )${2+ }$2";;
	esac
}

sane_sh

set -e

# USAGE:
#   scoped_command scope cmd msg var_suffix
#
# Write to stdout a piece of Bourne Shell script with _cmd_ in specific
# _scope_.  The execution of _cmd_ is bracketed by prints of "before _msg_"
# and "after _msg_, return value ${?}".  If the generated script uses
# variables, __var_suffix_ is appended to their names to allow nesting of
# scripts generated this way.
#
# _scope_ should be one of: case, compound, file, for, func, subshell,
# until, while.
# _cmd_ is the command line to execute.  Remember proper quoting!
# _msg_ is text that will be used inside single quotes.
# _var_suffix_ is a syntactically valid identifier name.

# don't rely on command lists (';')
cmd="echo 'before ${3}'
${2}
echo 'after ${3}, return value:' ${?}"

echo "#!${TEST_SH}"

[ 'func' = "${1}" ] && cat <<EOF
func()
{
    echo 'before ${3}'
    \${1}
    echo 'after ${3}'
}

echo 'before function'
func "${2}" "${3}"  # don't rely on 'shift'
echo 'after function'
EOF

[ 'case' = "${1}" ] && cat <<EOF
echo 'before case'
case 'a' in
    a)  ${cmd};;
esac
echo 'after case'
EOF

[ 'file' = "${1}" ] && cat <<EOF
${cmd}
EOF

[ 'while' = "${1}" ] && cat <<EOF
echo 'before while'
cond_${4}='true true false'
while \${cond_${4}}
do
    cond_${4}="\${cond_${4}#* }"
    ${cmd}
done
echo 'after while'
EOF

[ 'until' = "${1}" ] && cat <<EOF
echo 'before until'
cond_${4}='false false true'
until \${cond_${4}}
do
    cond_${4}="\${cond_${4}#* }"
    ${cmd}
done
echo 'after until'
EOF

[ 'for' = "${1}" ] && cat <<EOF
echo 'before for'
for i_${4} in 1 2
do
    ${cmd}
done
echo 'after for'
EOF

[ 'subshell' = "${1}" ] && cat <<EOF
(
    echo 'subshell start'
    ${cmd}
    echo 'subshell end'
)
EOF

[ 'compound' = "${1}" ] && cat <<EOF
{
    echo 'compound start'
    ${cmd};
    echo 'compound end'
}
EOF

exit 0
