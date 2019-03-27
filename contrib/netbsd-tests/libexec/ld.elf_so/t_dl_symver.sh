# $NetBSD: t_dl_symver.sh,v 1.1 2011/06/25 05:45:13 nonaka Exp $
#
# Copyright (c) 2011 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by NONAKA Kimihiro.
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

datadir() {
	echo "$(atf_get_srcdir)/data"
}

atf_test_case dl_symver
dl_symver_head()
{
	atf_set "descr" "Checks ELF symbol versioning functions"
}
dl_symver_body()
{
	for tv in 0 1 2; do
		for lv in 0 1 2; do 
			atf_check -s ignore \
			    -o file:$(datadir)/symver-output-ref-stdout.v$tv-v$lv \
			    -e file:$(datadir)/symver-output-ref-stderr.v$tv-v$lv \
			    -x "LD_LIBRARY_PATH=$(atf_get_srcdir)/h_helper_symver_dso$lv $(atf_get_srcdir)/h_dl_symver_v$tv"
		done
	done
}

atf_init_test_cases()
{
	atf_add_test_case dl_symver
}
