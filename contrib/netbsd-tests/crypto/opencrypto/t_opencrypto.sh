#	$NetBSD: t_opencrypto.sh,v 1.6 2015/12/26 07:10:03 pgoyette Exp $
#
# Copyright (c) 2014 The NetBSD Foundation, Inc.
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

# Start a rumpserver, load required modules, and set requires sysctl vars

start_rump() {
	rump_libs="-l rumpvfs -l rumpdev -l rumpdev_opencrypto"
	rump_libs="${rump_libs} -l rumpkern_z -l rumpkern_crypto"

	rump_server ${rump_libs} ${RUMP_SERVER} || \
	    return 1

	rump.sysctl -w kern.cryptodevallowsoft=-1 && \
	    return 0

	rump.halt

	return 1
}

common_head() {
	atf_set	descr		"$1"
	atf_set	timeout		10
	atf_set	require.progs	rump_server	rump.sysctl	rump.halt
}

common_body() {
	local status

	start_rump || atf_skip "Cannot set-up rump environment"
	LD_PRELOAD="/usr/lib/librumphijack.so" ; export LD_PRELOAD
	RUMPHIJACK="blanket=/dev/crypto" ; export RUMPHIJACK
	$(atf_get_srcdir)/$1
	status=$?
	unset RUMPHIJACK
	unset LD_PRELOAD
	if [ $status -ne 0 ] ; then
		atf_fail "$1 returned non-zero status, check output/error"
	fi
}

common_cleanup() {
	unset RUMPHIJACK
	unset LD_PRELOAD
	rump.halt
}

atf_test_case arc4 cleanup
arc4_head() {
	common_head "Test ARC4 crypto"
}

arc4_body() {
	atf_skip "ARC4 not implemented by swcrypto"
	common_body h_arc4
}

arc4_cleanup() {
	# No cleanup required since test is skipped.  Trying to run rump.halt
	# at this point fails, causing the ATF environment to erroneously
	# report a failed test!
	#
	# common_cleanup
}

atf_test_case camellia cleanup
camellia_head() {
	common_head "Test CAMELLIA_CBC crypto"
}

camellia_body() {
	common_body h_camellia
}

camellia_cleanup() {
	common_cleanup
}

atf_test_case cbcdes cleanup
cbcdes_head() {
	common_head "Test DES_CBC crypto"
}

cbcdes_body() {
	common_body h_cbcdes
}

cbcdes_cleanup() {
	common_cleanup
}

atf_test_case comp cleanup
comp_head() {
	common_head "Test GZIP_COMP Compression"
}

comp_body() {
	common_body h_comp
}

comp_cleanup() {
	common_cleanup
}

atf_test_case comp_deflate cleanup
comp_deflate_head() {
	common_head "Test DEFLATE_COMP Compression"
}

comp_deflate_body() {
	common_body h_comp_zlib
}

comp_deflate_cleanup() {
	common_cleanup
}

atf_test_case comp_zlib_rnd cleanup
comp_zlib_rnd_head() {
	common_head "Test DEFLATE_COMP Compression with random data"
}

comp_zlib_rnd_body() {
	common_body h_comp_zlib_rnd
}

comp_zlib_rnd_cleanup() {
	common_cleanup
}

atf_test_case aesctr1 cleanup
aesctr1_head() {
	common_head "Test AES_CTR crypto"
}

aesctr1_body() {
	common_body h_aesctr1
}

aesctr1_cleanup() {
	common_cleanup
}

atf_test_case aesctr2 cleanup
aesctr2_head() {
	common_head "Test AES_CTR crypto"
}

aesctr2_body() {
	common_body h_aesctr2
}

aesctr2_cleanup() {
	common_cleanup
}

atf_test_case gcm cleanup
gcm_head() {
	common_head "Test AES_GCM_16 crypto"
}

gcm_body() {
	common_body h_gcm
}

gcm_cleanup() {
	common_cleanup
}

atf_test_case md5 cleanup
md5_head() {
	common_head "Test MD5 crypto"
}

md5_body() {
	common_body h_md5
}

md5_cleanup() {
	common_cleanup
}

atf_test_case md5_hmac cleanup
md5_hmac_head() {
	common_head "Test MD5_HMAC crypto"
}

md5_hmac_body() {
	common_body h_md5hmac
}

md5_hmac_cleanup() {
	common_cleanup
}

atf_test_case null cleanup
null_head() {
	common_head "Test NULL_CBC crypto"
}

null_body() {
	common_body h_null
}

null_cleanup() {
	common_cleanup
}

atf_test_case sha1_hmac cleanup
sha1_hmac_head() {
	common_head "Test SHA1_HMAC crypto"
}

sha1_hmac_body() {
	common_body h_sha1hmac
}

sha1_hmac_cleanup() {
	common_cleanup
}

atf_test_case xcbcmac cleanup
xcbcmac_head() {
	common_head "Test XCBC_MAC_96 crypto"
}

xcbcmac_body() {
	common_body h_xcbcmac
}

xcbcmac_cleanup() {
	common_cleanup
}

atf_init_test_cases() {
	RUMP_SERVER="unix://t_opencrypto_socket" ; export RUMP_SERVER

	atf_add_test_case arc4
	atf_add_test_case camellia
	atf_add_test_case cbcdes
	atf_add_test_case comp
	atf_add_test_case comp_deflate
	atf_add_test_case comp_zlib_rnd
	atf_add_test_case aesctr1
	atf_add_test_case aesctr2
	atf_add_test_case gcm
	atf_add_test_case md5
	atf_add_test_case md5_hmac
	atf_add_test_case null
	atf_add_test_case sha1_hmac
	atf_add_test_case xcbcmac
}
