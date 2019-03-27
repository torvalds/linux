# $NetBSD: t_config.sh,v 1.8 2016/08/27 12:08:14 christos Exp $
#
# Copyright (c) 2008, 2010 The NetBSD Foundation, Inc.
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

srcdir=..
merge_backslash()
{
	sed '
: again
/\\$/ {
    N
    s/\\\n//
    t again
}
' "$1"
}
run_and_check_prep()
{
	local name="${1}"; shift

	mkdir -p compile
	srcdir="$(atf_get_srcdir)"
	if [ ! -d "${srcdir}/support" ]; then
		srcdir="$(dirname "${srcdir}")"
		if [ ! -d "${srcdir}/support" ]; then
			atf_fail "bad source directory ${srcdir}"
			exit 1
		fi
	fi
	supportdir="${srcdir}/support"

	local config_str
	eval config_str=\$${name}_config_str
	if [ -n "$config_str" ]; then
		config="d_${name}"
		printf "$config_str" >"${config}"
	else
		config="${srcdir}/d_${name}"
	fi
}

run_and_check_pass()
{
	local name="${1}"; shift

	run_and_check_prep "${name}"

	atf_check -o ignore -s eq:0 \
	    config -s "${supportdir}" -b "compile/${name}" "${config}"
}

run_and_check_warn()
{
	local name="${1}"; shift

	run_and_check_prep "${name}"

	local stderr
	eval stderr=\$${name}_stderr
	atf_check -o ignore -e "${stderr}" -s eq:0 \
	    config -s "${supportdir}" -b "compile/${name}" "${config}"
}

run_and_check_fail()
{
	local name="${1}"; shift

	run_and_check_prep "${name}"

	atf_check -o ignore -e ignore -s ne:0 \
	    config -s "${supportdir}" -b "compile/${name}" "${config}"
}

test_output()
{
	local name="${1}"; shift
	local res=1

	run_and_check_prep "${name}"

	config -s "${supportdir}" -b compile/"${name}" "${config}" >/dev/null &&
	cd compile/"${name}" &&
	check_${name} &&
	cd $OLDPWD &&
	res=0

	atf_check test $res -eq 0
}

# Defines a test case for config(1).
test_case()
{
	local name="${1}"; shift
	local type="${1}"; shift
	local descr="${*}"

	atf_test_case "${name}"
	eval "${name}_head() { \
		atf_set descr \"${descr}\"; \
		atf_set require.progs \"config\"; \
	}"
	eval "${name}_body() { \
		run_and_check_${type} '${name}'; \
	}"
}

test_case shadow_instance pass "Checks correct handling of shadowed instances"
test_case loop pass "Checks correct handling of loops"
test_case loop2 pass "Checks correct handling of devices that can be their" \
    "own parents"
test_case pseudo_parent pass "Checks correct handling of children of pseudo" \
    "devices (PR/32329)"
test_case postponed_orphan fail "Checks that config catches adding an" \
    "instance of a child of a negated instance as error"
test_case no_pseudo fail "Checks that config catches ommited 'pseudo-device'" \
    "as error (PR/34111)"
test_case deffs_redef fail "Checks that config doesn't allow a deffs to use" \
    "the same name as a previous defflag/defparam"

# Selecting an undefined option.
undefined_opt_config_str="
include \"${srcdir}/d_min\"
options UNDEFINED
"
test_case undefined_opt pass \
    "Checks that config allows a selection for an undefined options"

# Negating an undefined option.
no_undefined_opt_config_str="
include \"${srcdir}/d_min\"
no options UNDEFINED
"
no_undefined_opt_stderr='match:UNDEFINED'
test_case no_undefined_opt warn \
    "Checks that config allows a negation for an undefined options"

# Attribute selection
test_case select pass "Attribute selection"
select_config_str="
include \"${srcdir}/d_min\"
select c
"
check_select()
{
	local f=Makefile

	grep -q '^	a\.c ' $f &&
	grep -q '^	b\.c ' $f &&
	grep -q '^	c\.c ' $f &&
	:
}
select_body() {
	test_output select
}

# Attribute negation
test_case no_select pass "Attribute negation"
no_select_config_str="
include \"${srcdir}/d_min\"
select c
no select a
"
check_no_select()
{
	local f=Makefile

	: >tmp
	grep -q '^a\.o:' $f >>tmp
	grep -q '^b\.o:' $f >>tmp
	grep -q '^c\.o:' $f >>tmp

	[ ! -s tmp ] &&
	:
}
no_select_body() {
	test_output no_select
}

# Device instance
test_case devi pass "Device instance"
devi_config_str="
include \"${srcdir}/d_min\"
d0 at root
"
check_devi()
{
	local f=ioconf.c

	sed -ne '/^struct cfdriver \* const cfdriver_list_initial\[\]/,/^};/p' $f >tmp.cfdriver
	sed -ne '/^struct cfdata cfdata\[\]/,/^};/p' $f >tmp.cfdata

	grep -q '^CFDRIVER_DECL(d, ' $f &&
	grep -q '&d_cd,' tmp.cfdriver &&
	grep -q '^extern struct cfattach d_ca;$' $f &&
	grep -q '^static const struct cfiattrdata \* const d_attrs\[\]' $f &&
	grep -q '^static const struct cfiattrdata icf_iattrdata' $f &&
	grep -q '{ "d",' tmp.cfdata &&
	:
}
devi_body() {
	test_output devi
}

# Check minimal kernel config(1) output
test_case min pass "Minimal config"
check_min_files()
{
	test -e Makefile &&
	test -e config_file.h &&
	test -e config_time.src &&
	test -e ioconf.c &&
	test -e ioconf.h &&
	test -e locators.h &&
	test -e swapregress.c &&
	test -h machine &&
	test -h regress &&
	:
}
check_min_makefile()
{
	local f=Makefile

	grep -q '^%' $f >tmp.template

	grep -q '^MACHINE=regress$' $f &&
	(merge_backslash $f | grep -q '^IDENT=[ 	]*-DMAXUSERS="4"') &&
	[ ! -s tmp.template ] &&
	:
}
check_min()
{
	check_min_files &&
	check_min_makefile &&
	:
}
min_body() {
	test_output min
}

atf_init_test_cases()
{
	atf_add_test_case shadow_instance
	atf_add_test_case loop
	atf_add_test_case loop2
	atf_add_test_case pseudo_parent
	atf_add_test_case postponed_orphan
	atf_add_test_case no_pseudo
	atf_add_test_case deffs_redef
	atf_add_test_case undefined_opt
	atf_add_test_case no_undefined_opt
	atf_add_test_case select
	atf_add_test_case no_select
	atf_add_test_case devi
	atf_add_test_case min
}
