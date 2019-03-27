#
# Copyright 2014 EMC Corp.
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
#
# $FreeBSD$

SRCDIR=$(atf_get_srcdir)

check()
{
	local tc=${1}; shift

	export SH=$(atf_config_get bin.sh.test_shell /bin/sh)

	local err_file="${SRCDIR}/${tc}.stderr"
	[ -f "${err_file}" ] && err_flag="-e file:${err_file}"
	local out_file="${SRCDIR}/${tc}.stdout"
	[ -f "${out_file}" ] && out_flag="-o file:${out_file}"

	atf_check -s exit:${tc##*.} ${err_flag} ${out_flag} ${SH} "${SRCDIR}/${tc}"
}

add_testcase()
{
	local tc=${1}
	local tc_escaped word

	case "${tc%.*}" in
	*-*)
		local IFS="-"
		for word in ${tc%.*}; do
			tc_escaped="${tc_escaped:+${tc_escaped}_}${word}"
		done
		;;
	*)
		tc_escaped=${tc%.*}
		;;
	esac

	atf_test_case ${tc_escaped}
	eval "${tc_escaped}_body() { check ${tc}; }"
	atf_add_test_case ${tc_escaped}
}

atf_init_test_cases()
{
	for path in $(find -Es "${SRCDIR}" -regex '.*\.[0-9]+$'); do
		add_testcase ${path##*/}
	done
}
