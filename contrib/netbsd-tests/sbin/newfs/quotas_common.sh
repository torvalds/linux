# $NetBSD: quotas_common.sh,v 1.3 2012/03/18 09:31:50 jruoho Exp $

#  Copyright (c) 2011 Manuel Bouyer
#  All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions
#  are met:
#  1. Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#  2. Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
#  THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
#  ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
#  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
#  PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
#  BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
#  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
#  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
#  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
#  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
#  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
#  POSSIBILITY OF SUCH DAMAGE.
#

create_with_quotas()
{
	local endian=$1; shift
	local vers=$1; shift
	local type=$1; shift
	local op;
	if [ ${type} = "both" ]; then
		op="-q user -q group"
	else
		op="-q ${type}"
	fi
	atf_check -o ignore -e ignore newfs ${op} \
		-B ${endian} -O ${vers} -s 4000 -F ${IMG}
}

# from tests/ipf/h_common.sh via tests/sbin/resize_ffs
test_case()
{
	local name="${1}"; shift
	local check_function="${1}"; shift
	local descr="${1}"; shift

	atf_test_case "${name}"

	eval "${name}_head() { \
		atf_set "descr" "Checks ${descr} quotas inodes"
	}"
	eval "${name}_body() { \
		${check_function} " "${@}" "; \
	}"
	tests="${tests} ${name}"
}

atf_init_test_cases()
{
	IMG=fsimage
	DIR=target
	for i in ${tests}; do
		atf_add_test_case $i
	done
}
