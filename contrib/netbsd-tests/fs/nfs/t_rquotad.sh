# $NetBSD: t_rquotad.sh,v 1.5 2016/08/10 23:25:39 kre Exp $ 
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
  for v in 1; do
    for q in "user" "group" "both"; do
	test_case_root get_nfs_${e}_${v}_${q} get_nfs_quota \
		"get NFS quota with ${q} enabled" ${e} ${v} ${q}
    done
  done
done

get_nfs_quota()
{
	create_ffs $*
	local q=$3
	local expect

	case ${q} in
	user)
		expect=u
		;;
	group)
		expect=g
		;;
	both)
		expect="u g"
		;;
	*)
		atf_fail "wrong quota type"
		;;
	esac

#start a a nfs server

	atf_check -s exit:0 rump_server -lrumpvfs -lrumpdev -lrumpnet   \
	    -lrumpnet_net -lrumpnet_netinet -lrumpnet_netinet6          \
	    -lrumpnet_local -lrumpnet_shmif -lrumpdev_disk -lrumpfs_ffs \
	    -lrumpfs_nfs -lrumpfs_nfsserver                             \
	    -d key=/dk,hostpath=${IMG},size=host ${RUMP_SERVER}

	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr shmbus
	atf_check -s exit:0 rump.ifconfig shmif0 inet 10.1.1.1

	export RUMPHIJACK_RETRYCONNECT=die
	export LD_PRELOAD=/usr/lib/librumphijack.so

	atf_check -s exit:0 mkdir /rump/etc
	atf_check -s exit:0 mkdir /rump/export
	atf_check -s exit:0 mkdir -p /rump/var/run
	atf_check -s exit:0 mkdir -p /rump/var/db
	atf_check -s exit:0 touch /rump/var/db/mountdtab

	/bin/echo "/export -noresvport -noresvmnt 10.1.1.100" | \
		dd of=/rump/etc/exports 2> /dev/null

	atf_check -s exit:0 -e ignore mount_ffs /dk /rump/export

#set a quota limit (and check that we can read it back)
	for q in ${expect} ; do
		local id=$(id -${q})
		atf_check -s exit:0 \
		   env LD_PRELOAD=/usr/lib/librumphijack.so RUMPHIJACK=vfs=getvfsstat,blanket=/export edquota -$q -s10k/20 -h40M/50k \
		   -t 2W/3D ${id}
		atf_check -s exit:0 \
-o "match:0       10    40960  2weeks       1      20   51200   3days" \
-o "match:Disk quotas for .*: $" \
		    env LD_PRELOAD=/usr/lib/librumphijack.so RUMPHIJACK=vfs=getvfsstat,blanket=/export quota -${q} -v
	done

	# start rpcbind.  we want /var/run/rpcbind.sock
	export RUMPHIJACK='blanket=/var/run,socket=all'
	atf_check -s exit:0 rpcbind

	# ok, then we want mountd in the similar fashion
	export RUMPHIJACK='blanket=/var/run:/var/db:/export,socket=all,path=/rump,vfs=all'
	atf_check -s exit:0 mountd /rump/etc/exports

	# and nfs
	export RUMPHIJACK='blanket=/var/run,socket=all,vfs=all'
	atf_check -s exit:0 nfsd

	#finally, rpc.rquotad
	export RUMPHIJACK='blanket=/var/run:/export,vfs=getvfsstat,socket=all'
	atf_check -s exit:0 /usr/libexec/rpc.rquotad

	# now start a client server
	export RUMP_SERVER=unix://clientsock
	RUMP_SOCKETS_LIST="${RUMP_SOCKETS_LIST} clientsock"
	unset RUMPHIJACK
	unset LD_PRELOAD

	atf_check -s exit:0 rump_server -lrumpvfs -lrumpnet -lrumpdev   \
            -lrumpnet_net -lrumpnet_netinet -lrumpnet_shmif -lrumpfs_nfs\
            ${RUMP_SERVER}

        atf_check -s exit:0 rump.ifconfig shmif0 create
        atf_check -s exit:0 rump.ifconfig shmif0 linkstr shmbus
        atf_check -s exit:0 rump.ifconfig shmif0 inet 10.1.1.100

        export LD_PRELOAD=/usr/lib/librumphijack.so

        atf_check -s exit:0 mkdir /rump/mnt
        atf_check -s exit:0 mount_nfs 10.1.1.1:/export /rump/mnt

	#now try a quota(8) call
	export RUMPHIJACK='blanket=/mnt,socket=all,path=/rump,vfs=getvfsstat'
	for q in ${expect} ; do
		local id=$(id -${q})
		atf_check -s exit:0 \
-o "match:/mnt        0       10    40960               1      20   51200        " \
-o "match:Disk quotas for .*: $" \
		    quota -${q} -v
	done

	unset LD_PRELOAD
	rump_quota_shutdown
}
