# Copyright 2012 Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# * Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the distribution.
# * Neither the name of Google Inc. nor the names of its contributors
#   may be used to endorse or promote products derived from this software
#   without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

atf_test_case defaults__build_and_install
defaults__build_and_install_head() {
	atf_set "require.progs" "/usr/bin/mandoc"
}
defaults__build_and_install_body() {
	if [ ! -e /usr/bin/gcc -a -e /usr/bin/clang ]; then
		export HAVE_LLVM=yes
	fi

	cat >hello.c <<EOF
#include <stdio.h>
int main(void) { printf("Hello, test!\n"); return 0; }
EOF
	cat >hello.1 <<EOF
Manpage of hello(1).
EOF

	cat >Makefile <<EOF
BINDIR = /the/bin/dir
PROG = hello
.include <bsd.prog.mk>
EOF

	atf_check -o ignore make
	mkdir -p root/the/bin/dir
	mkdir -p root/usr/share/man/man1
	mkdir -p root/usr/share/man/html1
	create_make_conf mk.conf owngrp DESTDIR="$(pwd)/root"
	atf_check -o ignore make install

	atf_check -o inline:'Hello, test!\n' ./root/the/bin/dir/hello
	atf_check -o inline:'Manpage of hello(1).\n' \
	    cat root/usr/share/man/man1/hello.1
	atf_check -o match:'Manpage of hello' \
	    cat root/usr/share/man/html1/hello.html
}

atf_test_case without_man__build_and_install
without_man__build_and_install_body() {
	if [ ! -e /usr/bin/gcc -a -e /usr/bin/clang ]; then
		export HAVE_LLVM=yes
	fi

	cat >hello.c <<EOF
#include <stdio.h>
int main(void) { printf("Hello, test!\n"); return 0; }
EOF

	cat >Makefile <<EOF
BINDIR = /the/bin/dir
PROG = hello
MAN =
.include <bsd.prog.mk>
EOF

	atf_check -o ignore make
	mkdir -p root/the/bin/dir
	create_make_conf mk.conf owngrp DESTDIR="$(pwd)/root"
	atf_check -o ignore make install

	atf_check -o inline:'Hello, test!\n' ./root/the/bin/dir/hello
}

atf_init_test_cases() {
	atf_add_test_case defaults__build_and_install
	atf_add_test_case without_man__build_and_install
}
