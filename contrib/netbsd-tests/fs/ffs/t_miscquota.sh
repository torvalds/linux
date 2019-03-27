# $NetBSD: t_miscquota.sh,v 1.8 2013/01/22 06:24:11 dholland Exp $ 
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

test_case_root walk_list_user quota_walk_list \
    "walk user quota list over several disk blocks" -b le 1 user

test_case_root psnapshot_user quota_snap \
    "create a persistent shapshot of quota-enabled fs, and do some writes" \
    -b le 1 user

test_case_root npsnapshot_user quota_snap \
    "create a non-persistent shapshot of quota-enabled fs, and do some writes" \
    -boL le 1 user

test_case_root psnapshot_unconf_user quota_snap \
    "create a persistent shapshot of quota-enabled fs, and do some writes and unconf" \
    -boC le 1 user

test_case_root npsnapshot_unconf_user quota_snap \
    "create a non-persistent shapshot of quota-enabled fs, and do some writes and unconf" \
    -boLC le 1 user

test_case log_unlink quota_log \
    "an unlinked file cleaned by the log replay should update quota" \
    -l le 1 user

test_case log_unlink_remount quota_log \
    "an unlinked file cleaned by the log replay after remount" \
    -oRL le 1 user


test_case_root default_deny_user quota_default_deny \
    "new quota entry denied by default entry" 5 -b le 1 user

test_case_root default_deny_user_big quota_default_deny \
    "new quota entry denied by default entry, with list on more than one block" 5000 -b le 1 user


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
	*)
		atf_fail "wrong quota type"
		;;
	esac

	# create 100 users, all in the same hash list
	local i=1;
	while [ $i -lt 101 ]; do
		atf_check -s exit:0 \
		   env LD_PRELOAD=/usr/lib/librumphijack.so RUMPHIJACK=vfs=getvfsstat,blanket=/mnt edquota -${expect} \
		   -s10k/20 -h40M/50k -t 2W/3D $((i * 4096))
		i=$((i + 1))
	done
	# do a repquota
	atf_check -s exit:0 -o 'match:user 409600 block  *81920 20 0' \
	    env LD_PRELOAD=/usr/lib/librumphijack.so RUMPHIJACK=vfs=getvfsstat,blanket=/mnt repquota -x -${expect} /mnt
	rump_quota_shutdown
}

quota_snap()
{
	local flag=$1; shift
	create_ffs $*
	local q=$3
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
	*)
		atf_fail "wrong quota type"
		;;
	esac

	#start our server which takes a snapshot
	atf_check -s exit:0 -o ignore \
	    $(atf_get_srcdir)/h_quota2_tests ${flag} 4 ${IMG} ${RUMP_SERVER}
	# create a few users
	local i=1;
	while [ $i -lt 11 ]; do
		atf_check -s exit:0 \
		   env LD_PRELOAD=/usr/lib/librumphijack.so RUMPHIJACK=vfs=getvfsstat,blanket=/mnt edquota -${expect} \
		   -s10k/20 -h40M/50k -t 2W/3D $i
		i=$((i + 1))
	done
	# we should have 5 files (root + 4 regular files)
	atf_check -s exit:0 \
	    -o 'match:-        -  7days         5       -       -  7days' \
	    env LD_PRELOAD=/usr/lib/librumphijack.so RUMPHIJACK=vfs=getvfsstat,blanket=/mnt repquota -av
	#shutdown and check filesystem
	rump_quota_shutdown
}

quota_log()
{
	local srv2args=$1; shift
	create_ffs $*
	local q=$3
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
	*)
		atf_fail "wrong quota type"
		;;
	esac

	#start our server which create a file and unlink while keeping
	# it open. The server halts itself without flush
	atf_check -s exit:0 -o ignore \
	    $(atf_get_srcdir)/h_quota2_tests -loU 5 ${IMG} ${RUMP_SERVER}
	# we should have one unlinked file, but the log covers it.
	atf_check -s exit:0 -o match:'3 files' -e ignore \
	    fsck_ffs -nf -F ${IMG}
	# have a kernel mount the fs again; it should cleanup the
	# unlinked file
	atf_check -o ignore -e ignore $(atf_get_srcdir)/h_quota2_tests \
	    ${srv2args} -b 5 ${IMG} ${RUMP_SERVER}
	#shutdown and check filesystem
	rump_quota_shutdown
}

quota_default_deny()
{
	local nusers=$1; shift
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
	*)
		atf_fail "wrong quota type"
		;;
	esac

	# create $nusers users, so we are sure the free list has entries
	# from block 1. Start from 10, as non-root id is 1.
	# set default to deny all
	( echo "@format netbsd-quota-dump v1"
	  echo "# idtype id objtype   hard soft usage expire grace"
	  echo "$q default block   0 0 0 0 0"
	  echo "$q default file   0 0 0 0 0"
	  local i=10;
	  while [ $i -lt $(($nusers + 10)) ]; do
		echo "$q $i block   0 0 0 0 0"
		echo "$q $i file   0 0 0 0 0"
		i=$((i + 1))
	  done
	) | atf_check -s exit:0 \
		   env LD_PRELOAD=/usr/lib/librumphijack.so RUMPHIJACK=vfs=getvfsstat,blanket=/mnt quotarestore -d /mnt
	atf_check -s exit:0 rump.halt
	#now start the server which does the limits tests
	$(atf_get_srcdir)/h_quota2_tests -oC -b 0 ${IMG} ${RUMP_SERVER}
	rump_quota_shutdown
}
