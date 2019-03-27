# $NetBSD: t_servent.sh,v 1.2 2016/03/08 08:34:17 joerg Exp $
#
# Copyright (c) 2008 The NetBSD Foundation, Inc.
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

atf_test_case servent
servent_head()
{
	atf_set "descr" "Checks {get,set,end}servent(3)"
	# libc doesn't include aliases
	atf_set "require.files" "/var/db/services.cdb" 
}
servent_body()
{
	#
	# Munge original to:
	#  (1) match output format of the test program
	#  (2) fold all names for the same port/proto together
	#  (3) prune duplicates
	#
	tr '\t' ' ' </etc/services | awk '
		function add(key, name,      i, n, ar) {
		n = split(names[key], ar);
		for (i=1; i<=n; i++) {
			if (name == ar[i]) {
			return;
			}
		}
		delete ar;
		names[key] = names[key] " " name;
		}

		{
		sub("#.*", "", $0);
		gsub("  *", " ", $0);
		if (NF==0) {
			next;
		}
		add($2, $1, 0);
		for (i=3; i<=NF; i++) {
			add($2, $i, 1);
		}
		}
		END {
		for (key in names) {
			portproto = key;
			sub("/", ", proto=", portproto);
			portproto = "port=" portproto;

			n = split(names[key], ar);
			printf "name=%s, %s, aliases=", ar[1], portproto;
			for (i=2; i<=n; i++) {
			if (i>2) {
				printf " ";
			}
			printf "%s", ar[i];
			}
			printf "\n";
			delete ar;
		}
		}
	' | sort >exp

	case "$(uname)" in
	FreeBSD)
		#  (3) Don't prune duplicates
		tr '\t' ' ' < /etc/services |
		    sed 's/#.*//;s/   */ /g; /^$/d;s#\([0-9]\)/#\1 #;s/ *$//' |
		    sort |
		    while read l; do
			set $l
			name=$1; shift
			port=$1; shift
			proto=$1; shift
			alias="$@"
			printf "name=%s, port=%s, proto=%s, aliases=%s\n" \
			    $name $port $proto "$alias"
		    done > exp
		;;
	esac

	# run test program
	"$(atf_get_srcdir)/h_servent" | sed 's/ *$//' | sort >out

	diff -u exp out || atf_fail "Observed output does not match reference output"
}

atf_init_test_cases()
{
	atf_add_test_case servent
}
