# $NetBSD: t_abi_uvm.sh,v 1.3 2012/04/20 05:41:25 jruoho Exp $
#
# Copyright (c) 2012 The NetBSD Foundation, Inc.
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

atf_test_case PAGE_SIZE cleanup
PAGE_SIZE_head() {
	atf_set "descr" "Ensures that modules have access to PAGE_SIZE"
	atf_set "require.user" "root"
}
PAGE_SIZE_body() {

	# XXX: Adjust when modctl(8) fails consistently.
	#
	$(atf_get_srcdir)/k_helper3 \
		"%s/k_helper/k_helper.kmod" $(atf_get_srcdir)

	if [ $? -eq 1 ] || [ $? -eq 78 ]; then
		atf_skip "host does not support modules"
	fi

	if modload $(atf_get_srcdir)/k_uvm/k_uvm.kmod; then
		:
	else
		case "$(uname -m)" in
		macppc)
			atf_expect_fail "PR port-macppc/46041"
			;;
		esac
		atf_fail "Failed to load k_uvm; missing uvmexp_pagesize?"
	fi

	kernel_pagesize="$(sysctl -n hw.pagesize || echo fail1)"
	module_pagesize="$(sysctl -n vendor.k_uvm.page_size || echo fail2)"
	echo "Kernel PAGE_SIZE: ${kernel_pagesize}"
	echo "Module PAGE_SIZE: ${module_pagesize}"
	atf_check_equal "${kernel_pagesize}" "${module_pagesize}"

	atf_check -s eq:0 -o empty -e empty modunload k_uvm
}
PAGE_SIZE_cleanup() {
	modunload k_uvm >/dev/null 2>&1 || true
}

atf_init_test_cases()
{
	atf_add_test_case PAGE_SIZE
}
