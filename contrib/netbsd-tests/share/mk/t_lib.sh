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
defaults__build_and_install_body() {
	create_c_module module1 first
	create_c_module module2 second

	CC=gcc
	if [ ! -e /usr/bin/gcc -a -e /usr/bin/clang ]; then
		export HAVE_LLVM=yes
		CC=clang
	fi

	cat >Makefile <<EOF
LIB = two-modules
SRCS = module1.c module2.c
.include <bsd.lib.mk>
EOF

	atf_check -o ignore make
	mkdir -p root/usr/lib
	mkdir -p root/usr/libdata/lint
	create_make_conf mk.conf owngrp DESTDIR="$(pwd)/root"
	atf_check -o ignore make install

	create_main_using_modules main.c module1.h:first module2.h:second
	atf_check -o ignore ${CC} -I. -Lroot/usr/lib -o main main.c -ltwo-modules

	atf_check -o inline:'module1\nmodule2\n' ./main
}

atf_init_test_cases() {
	atf_add_test_case defaults__build_and_install
}
