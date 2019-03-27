# $NetBSD: t_filter_parse.sh,v 1.12 2014/12/06 19:31:25 dholland Exp $
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
#
# (C)opyright 1993-1996 by Darren Reed.
#
# See the IPFILTER.LICENCE file for details on licencing.
#

itest()
{
	h_copydata $1

	case $3 in
	ipf)
		atf_check -o file:exp -e ignore ipf -Rnvf reg
		;;
	ipftest)
		atf_check -o file:exp ipftest -D -r reg -i /dev/null
		;;
	esac
}

itest_i19()
{
	cp "$(atf_get_srcdir)/expected/i19.dist" .

	if [ "`grep LOG_SECURITY /usr/include/sys/syslog.h 2>&1`" = "" ] ; then
		if [ "`grep LOG_AUDIT /usr/include/sys/syslog.h 2>&1`" = "" ] ; then
				sed -e 's/security/!!!/g' i19.dist > i19.p1;
		else
				sed -e 's/security/audit/g' i19.dist > i19.p1;
		fi
	else
		cp i19.dist i19.p1;
	fi
	if [ "`grep LOG_AUTHPRIV /usr/include/sys/syslog.h 2>&1`" = "" ] ; then
		sed -e 's/authpriv/!!!/g' i19.p1 > i19.p2;
	else
		cp i19.p1 i19.p2;
	fi
	if [ "`grep LOG_LOGALERT /usr/include/sys/syslog.h 2>&1`" = "" ] ; then
		sed -e 's/logalert/!!!/g' i19.p2 > i19.p1;
	else
		cp i19.p2 i19.p1;
	fi
	if [ "`grep LOG_FTP /usr/include/sys/syslog.h 2>&1`" = "" ] ; then
		sed -e 's/ftp/!!!/g' i19.p1 > i19.p2;
	else
		cp i19.p1 i19.p2;
	fi
	if [ "`egrep 'LOG_CRON.*15' /usr/include/sys/syslog.h 2>&1`" != "" ] ; then
		sed -e 's/cron/cron2/g' i19.p2 > i19;
	else
		cp i19.p2 i19;
	fi
	/bin/rm i19.p?

	mv i19 exp
	itest "$@"
}

test_case i1 itest text ipf
test_case i2 itest text ipf
test_case i3 itest text ipf
test_case i4 itest text ipf
test_case i5 itest text ipf
test_case i6 itest text ipf
test_case i7 itest text ipf
test_case i8 itest text ipf
test_case i9 itest text ipf
test_case i10 itest text ipf
test_case i11 itest text ipf
test_case i12 itest text ipf
test_case i13 itest text ipf
test_case i14 itest text ipf
test_case i15 itest text ipf
test_case i16 itest text ipf
failing_test_case i17 itest "Known to be broken" text ipftest
test_case i18 itest text ipf
test_case i19 itest_i19 text ipf
test_case i20 itest text ipf
test_case i21 itest text ipf
test_case i22 itest text ipf
test_case i23 itest text ipf

atf_init_test_cases()
{
	atf_add_test_case i1
	atf_add_test_case i2
	atf_add_test_case i3
	atf_add_test_case i4
	atf_add_test_case i5
	atf_add_test_case i6
	atf_add_test_case i7
	atf_add_test_case i8
	atf_add_test_case i9
	atf_add_test_case i10
	atf_add_test_case i11
	atf_add_test_case i12
	atf_add_test_case i13
	atf_add_test_case i14
	atf_add_test_case i15
	atf_add_test_case i16
	atf_add_test_case i17
	atf_add_test_case i18
	atf_add_test_case i19
	atf_add_test_case i20
	atf_add_test_case i21
	atf_add_test_case i22
	atf_add_test_case i23
}

