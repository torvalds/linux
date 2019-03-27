# $NetBSD: t_modload.sh,v 1.13 2012/04/20 05:41:25 jruoho Exp $
#
# Copyright (c) 2008 The NetBSD Foundation, Inc.
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

check_sysctl() {
	echo "${1} = ${2}" >expout
	atf_check -s eq:0 -o file:expout -e empty sysctl ${1}
}

atf_test_case plain cleanup
plain_head() {
	atf_set "descr" "Test load without arguments"
	atf_set "require.user" "root"
}
plain_body() {

	# XXX: Adjust when modctl(8) fails consistently.
	#
	$(atf_get_srcdir)/k_helper3 \
		"%s/k_helper/k_helper.kmod" $(atf_get_srcdir)

	if [ $? -eq 1 ] || [ $? -eq 78 ]; then
		atf_skip "host does not support modules"
	fi

	cat >experr <<EOF
modload: No such file or directory
EOF
	atf_check -s eq:1 -o empty -e ignore modload non-existent.o

	atf_check -s eq:0 -o empty -e empty \
	    modload $(atf_get_srcdir)/k_helper/k_helper.kmod
	check_sysctl vendor.k_helper.present 1
	check_sysctl vendor.k_helper.prop_int_ok 0
	check_sysctl vendor.k_helper.prop_str_ok 0
	atf_check -s eq:0 -o empty -e empty modunload k_helper
	touch done
}
plain_cleanup() {
	test -f done || modunload k_helper >/dev/null 2>&1
}

atf_test_case bflag cleanup
bflag_head() {
	atf_set "descr" "Test the -b flag"
	atf_set "require.user" "root"
}
bflag_body() {
	echo "Checking error conditions"

	atf_check -s eq:1 -o empty -e save:stderr \
	    modload -b foo k_helper.kmod
	atf_check -s eq:0 -o ignore -e empty \
	    grep 'Invalid parameter.*foo' stderr

	atf_check -s eq:1 -o empty -e save:stderr \
	    modload -b foo= k_helper.kmod
	atf_check -s eq:0 -o ignore -e empty \
	    grep 'Invalid boolean value' stderr

	atf_check -s eq:1 -o empty -e save:stderr \
	    modload -b foo=bar k_helper.kmod
	atf_check -s eq:0 -o ignore -e empty \
	    grep 'Invalid boolean value.*bar' stderr

	atf_check -s eq:1 -o empty -e save:stderr \
	    modload -b foo=falsea k_helper.kmod
	atf_check -s eq:0 -o ignore -e empty \
	    grep 'Invalid boolean value.*falsea' stderr

	atf_check -s eq:1 -o empty -e save:stderr \
	    modload -b foo=truea k_helper.kmod
	atf_check -s eq:0 -o ignore -e empty \
	    grep 'Invalid boolean value.*truea' stderr

	# TODO Once sysctl(8) supports CTLTYPE_BOOL nodes.
	#echo "Checking valid values"
}
bflag_cleanup() {
	modunload k_helper >/dev/null 2>&1 || true
}

atf_test_case iflag cleanup
iflag_head() {
	atf_set "descr" "Test the -i flag"
	atf_set "require.user" "root"
}
iflag_body() {

	# XXX: Adjust when modctl(8) fails consistently.
	#
	$(atf_get_srcdir)/k_helper3 \
		"%s/k_helper/k_helper.kmod" $(atf_get_srcdir)

	if [ $? -eq 1 ] || [ $? -eq 78 ]; then
		atf_skip "host does not support modules"
	fi

	echo "Checking error conditions"

	atf_check -s eq:1 -o empty -e save:stderr modload -i foo \
	    k_helper/k_helper.kmod
	atf_check -s eq:0 -o ignore -e empty \
	    grep 'Invalid parameter.*foo' stderr

	atf_check -s eq:1 -o empty -e save:stderr modload -i foo= \
	    k_helper/k_helper.kmod
	atf_check -s eq:0 -o ignore -e empty \
	    grep 'Invalid integer value' stderr

	atf_check -s eq:1 -o empty -e save:stderr \
	    modload -i foo=bar k_helper/k_helper.kmod
	atf_check -s eq:0 -o ignore -e empty \
	    grep 'Invalid integer value.*bar' stderr

	atf_check -s eq:1 -o empty -e save:stderr \
	    modload -i foo=123a k_helper/k_helper.kmod
	atf_check -s eq:0 -o ignore -e empty \
	    grep 'Invalid integer value.*123a' stderr

	echo "Checking valid values"

	for v in 5 10; do
		atf_check -s eq:0 -o empty -e empty \
		    modload -i prop_int="${v}" \
		    $(atf_get_srcdir)/k_helper/k_helper.kmod
		check_sysctl vendor.k_helper.prop_int_ok 1
		check_sysctl vendor.k_helper.prop_int_val "${v}"
		atf_check -s eq:0 -o empty -e empty modunload k_helper
	done
	touch done
}
iflag_cleanup() {
	test -f done || modunload k_helper >/dev/null 2>&1
}

atf_test_case sflag cleanup
sflag_head() {
	atf_set "descr" "Test the -s flag"
	atf_set "require.user" "root"
}
sflag_body() {

	# XXX: Adjust when modctl(8) fails consistently.
	#
	$(atf_get_srcdir)/k_helper3 \
		"%s/k_helper/k_helper.kmod" $(atf_get_srcdir)

	if [ $? -eq 1 ] || [ $? -eq 78 ]; then
		atf_skip "host does not support modules"
	fi

	echo "Checking error conditions"

	atf_check -s eq:1 -o empty -e save:stderr modload -s foo \
	    k_helper/k_helper.kmod
	atf_check -s eq:0 -o ignore -e empty \
	    grep 'Invalid parameter.*foo' stderr

	echo "Checking valid values"

	for v in '1st string' '2nd string'; do
		atf_check -s eq:0 -o empty -e empty \
		    modload -s prop_str="${v}" \
		    $(atf_get_srcdir)/k_helper/k_helper.kmod
		check_sysctl vendor.k_helper.prop_str_ok 1
		check_sysctl vendor.k_helper.prop_str_val "${v}"
		atf_check -s eq:0 -o empty -e empty modunload k_helper
	done
	touch done
}
sflag_cleanup() {
	test -f done || modunload k_helper >/dev/null 2>&1
}

atf_init_test_cases()
{
	atf_add_test_case plain
	atf_add_test_case bflag
	atf_add_test_case iflag
	atf_add_test_case sflag
}
