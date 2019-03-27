# $NetBSD: t_enable_quotas.sh,v 1.3 2012/03/18 09:31:50 jruoho Exp $
#
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

for e in le be; do
  for v in 1 2; do
    for q in "user" "group"; do
      test_case enabled_${e}_${v}_${q} quota_enabled_single \
	 "creation of" ${e} ${v} ${q}
    done
    test_case enabled_${e}_${v}_"both" quota_enabled_both \
	 "creation of" ${e} ${v} "both"
  done
done

quota_enabled_single()
{
	create_with_quotas $*

# check that the quota inode creation didn't corrupt the filesystem
	atf_check -s exit:0 -o "match:already clean" -o "match:2 files" \
		-o "match:Phase 6 - Check Quotas" \
		fsck_ffs -nf -F ${IMG}
}

quota_enabled_both()
{
	create_with_quotas $*

# check that the quota inode creation didn't corrupt the filesystem
	atf_check -s exit:0 -o "match:already clean" -o "match:3 files" \
		-o "match:Phase 6 - Check Quotas" \
		fsck_ffs -nf -F ${IMG}
}
