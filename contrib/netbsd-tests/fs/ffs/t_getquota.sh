# $NetBSD: t_getquota.sh,v 1.4 2012/01/18 20:51:23 bouyer Exp $ 
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
      test_case get_${e}_${v}_${q} get_quota \
	 "get quota with ${q} enabled" -b ${e} ${v} ${q}
    done
    test_case get_${e}_${v}_"both" get_quota \
	 "get quota with both enabled" -b ${e} ${v} "both"
  done
done

get_quota()
{
	create_ffs_server $*
	local q=$4
	local expect
	local fail

	case ${q} in
	user)
		expect=u
		fail=g
		;;
	group)
		expect=g
		fail=u
		;;
	both)
		expect="u g"
		fail=""
		;;
	*)
		atf_fail "wrong quota type"
		;;
	esac

#check that we can get the expected quota
	for q in ${expect} ; do
		atf_check -s exit:0 \
-o "match:/mnt        0        -        -   7days       1       -       -   7days" \
-o "match:Disk quotas for .*: $" \
		    env LD_PRELOAD=/usr/lib/librumphijack.so RUMPHIJACK=vfs=getvfsstat,blanket=/mnt quota -${q} -v
		atf_check -s exit:0 \
-o "match:--        0        -        -                1       -       -" \
-o "not-match:\+\+"							  \
		    env LD_PRELOAD=/usr/lib/librumphijack.so RUMPHIJACK=vfs=getvfsstat,blanket=/mnt repquota -${q} /mnt
	done

#check that we do not get positive reply for non-expected quota
	for q in ${fail} ; do
		atf_check -s exit:0 -o "not-match:/mnt" \
		    -o "not-match:Disk quotas for .*: $" \
		    -o "match:Disk quotas for .*: none$" \
		    env LD_PRELOAD=/usr/lib/librumphijack.so RUMPHIJACK=vfs=getvfsstat,blanket=/mnt quota -${q} -v
		atf_check -s exit:0 \
-o "not-match:--        0        -        -                1       -       -" \
		    env LD_PRELOAD=/usr/lib/librumphijack.so RUMPHIJACK=vfs=getvfsstat,blanket=/mnt repquota -${q} /mnt
	done
	rump_quota_shutdown
}

quota_walk_list()
{
	create_ffs_server $*
	local q=$4
	local expect

	case ${q} in
	user)
		expect=u
		fail=g
		;;
	group)
		expect=g
		fail=u
		;;
	both)
		expect="u g"
		fail=""
		;;
	*)
		atf_fail "wrong quota type"
		;;
	esac
}
