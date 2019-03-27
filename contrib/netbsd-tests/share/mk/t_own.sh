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

atf_test_case makeconf__ok
makeconf__ok_body() {
	cat >Makefile <<EOF
A_TEST_CONFIG_VARIABLE = not overriden

.PHONY: show-config-var
show-config-var:
	@echo \${A_TEST_CONFIG_VARIABLE}

.include <bsd.own.mk>
EOF

	echo >empty.conf
	cat >custom.conf <<EOF
A_TEST_CONFIG_VARIABLE = 'a value'
EOF
	atf_check -o inline:'not overriden\n' \
	    make MAKECONF="$(pwd)/empty.conf" show-config-var
	atf_check -o inline:'a value\n' \
	    make MAKECONF="$(pwd)/custom.conf" show-config-var
}

atf_test_case makeconf__missing
makeconf__missing_body() {
	cat >Makefile <<EOF
.PHONY: hello
hello:
	@echo 'Did not error out on a missing file!'

.include <bsd.own.mk>
EOF

	echo >empty.conf
	cat >custom.conf <<EOF
A_TEST_CONFIG_VARIABLE = 'a value'
EOF
	atf_check -o inline:'Did not error out on a missing file!\n' \
	    make MAKECONF="$(pwd)/non-existent.conf" hello
}

atf_init_test_cases() {
	atf_add_test_case makeconf__ok
	atf_add_test_case makeconf__missing
}
