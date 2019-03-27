# $NetBSD: t_asm.sh,v 1.1 2013/02/16 12:44:26 jmmv Exp $
#
# Copyright (c) 2011 The NetBSD Foundation, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
# PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

# check_implemented <example_name>
#
# Verifies if a particular asm example is implemented for the current
# platform.  The example_name argument is the name of the subdirectory
# of the examples/asm/ subtree that includes the code for the example
# under test.
#
# If the example is not implemented, the calling test is skipped.  If the
# check for implementation fails, the calling test is failed.
check_implemented() {
	local name="${1}"; shift

	local implemented=$(cd /usr/share/examples/asm/${name}/ && \
	                    make check-implemented)
	[ $? -eq 0 ] || atf_fail "Failed to determine if the sample" \
	    "program is supported"
	[ "${implemented}" = yes ] || atf_skip "Example program not" \
	    "implemented on this platform"
}

# copy_example <example_name>
#
# Copies the example code and supporting Makefiles into the current
# directory.
copy_example() {
	local name="${1}"; shift

	cp /usr/share/examples/asm/${name}/* .
}

atf_test_case hello
hello_head() {
	atf_set "descr" "Builds, runs and validates the 'hello' asm example"
	atf_set "require.files" "/usr/share/examples/asm/hello/"
	atf_set "require.progs" "make"
}
hello_body() {
	check_implemented hello
	copy_example hello
	atf_check -s exit:0 -o ignore -e ignore make
	atf_check -s exit:0 -o inline:'Hello, world!\n' -e empty ./hello
}

atf_init_test_cases() {
	atf_add_test_case hello
}
