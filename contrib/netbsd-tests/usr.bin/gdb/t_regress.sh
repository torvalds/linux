# $NetBSD: t_regress.sh,v 1.1 2016/04/08 10:09:16 gson Exp $
#
# Copyright (c) 2016 The NetBSD Foundation, Inc.
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

# Regression tests for some GDB PRs

# PR 47430

atf_test_case threads
threads_head() {
	atf_set "descr" "Test that gdb works with threaded programs"
	atf_set "require.progs" "gdb"
}
threads_body() {
	# Dig at an unused IP address so that dig fails the
	# same way on machines with Internet connectivity
	# as on those without.
	cat <<EOF >test.gdb
run +time=1 +tries=1 @127.0.0.177
cont
cont
cont
cont
cont
EOF
	gdb --batch -x test.gdb dig >gdb.out
	atf_check -s exit:1 -o ignore -e ignore grep "Program received signal SIGTRAP" gdb.out
}

# PR 48250

atf_test_case pie
pie_head() {
	atf_set "descr" "Test that gdb works with PIE executables"
	atf_set "require.progs" "cc gdb"
}
pie_body() {
	cat <<\EOF >test.c
#include <stdio.h>
int main(int argc, char **argv) { printf ("hello\n"); return 0; }
EOF
	cc -fpie -pie -g test.c -o test
	cat <<EOF >test.gdb
break main
run
EOF
	gdb --batch -x test.gdb ./test >gdb.out 2>&1
	atf_check -s exit:1 -o ignore -e ignore grep "annot access memory" gdb.out
}

atf_init_test_cases() {
	atf_add_test_case threads
	atf_add_test_case pie
}
