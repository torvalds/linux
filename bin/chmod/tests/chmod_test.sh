#
# Copyright (c) 2017 Dell EMC
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
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$

get_filesystem()
{
	local mountpoint=$1

	df -T $mountpoint | tail -n 1 | cut -wf 2
}

atf_test_case RH_flag
RH_flag_head()
{
	atf_set	"descr" "Verify that setting modes recursively via -R doesn't " \
			"affect symlinks specified via the arguments when -H " \
			"is specified"
}
RH_flag_body()
{
	atf_check mkdir -m 0777 -p A/B
	atf_check ln -s B A/C
	atf_check chmod -h 0777 A/C
	atf_check -o inline:'40755\n40777\n120777\n' stat -f '%p' A A/B A/C
	atf_check chmod -RH 0700 A
	atf_check -o inline:'40700\n40700\n120700\n' stat -f '%p' A A/B A/C
	atf_check chmod -RH 0600 A/C
	atf_check -o inline:'40700\n40600\n120700\n' stat -f '%p' A A/B A/C
}

atf_test_case RL_flag
RL_flag_head()
{
	atf_set	"descr" "Verify that setting modes recursively via -R doesn't " \
			"affect symlinks specified via the arguments when -L " \
			"is specified"
}
RL_flag_body()
{
	atf_check mkdir -m 0777 -p A/B
	atf_check ln -s B A/C
	atf_check chmod -h 0777 A/C
	atf_check -o inline:'40755\n40777\n120777\n' stat -f '%p' A A/B A/C
	atf_check chmod -RL 0700 A
	atf_check -o inline:'40700\n40700\n120777\n' stat -f '%p' A A/B A/C
	atf_check chmod -RL 0600 A/C
	atf_check -o inline:'40700\n40600\n120777\n' stat -f '%p' A A/B A/C
}

atf_test_case RP_flag
RP_flag_head()
{
	atf_set	"descr" "Verify that setting modes recursively via -R doesn't " \
			"affect symlinks specified via the arguments when -P " \
			"is specified"
}
RP_flag_body()
{
	atf_check mkdir -m 0777 -p A/B
	atf_check ln -s B A/C
	atf_check chmod -h 0777 A/C
	atf_check -o inline:'40755\n40777\n120777\n' stat -f '%p' A A/B A/C
	atf_check chmod -RP 0700 A
	atf_check -o inline:'40700\n40700\n120700\n' stat -f '%p' A A/B A/C
	atf_check chmod -RP 0600 A/C
	atf_check -o inline:'40700\n40700\n120600\n' stat -f '%p' A A/B A/C
}

atf_test_case f_flag cleanup
f_flag_head()
{
	atf_set	"descr" "Verify that setting a mode for a file with -f " \
			"doesn't emit an error message/exit with a non-zero " \
			"code"
}

f_flag_body()
{
	atf_check truncate -s 0 foo bar
	atf_check chmod 0750 foo bar
	case "$(get_filesystem .)" in
	zfs)
		atf_expect_fail "ZFS doesn't support UF_IMMUTABLE; returns EPERM - bug 221189"
		;;
	esac
	atf_check chflags uchg foo
	atf_check -e not-empty -s not-exit:0 chmod 0700 foo bar
	atf_check -o inline:'100750\n100700\n' stat -f '%p' foo bar
	atf_check -s exit:0 chmod -f 0600 foo bar
	atf_check -o inline:'100750\n100600\n' stat -f '%p' foo bar
}

f_flag_cleanup()
{
	chflags 0 foo || :
}

atf_test_case h_flag
h_flag_head()
{
	atf_set	"descr" "Verify that setting a mode for a file with -f " \
			"doesn't emit an error message/exit with a non-zero " \
			"code"
}

h_flag_body()
{
	atf_check truncate -s 0 foo
	atf_check chmod 0600 foo
	atf_check -o inline:'100600\n' stat -f '%p' foo
	umask 0077
	atf_check ln -s foo bar
	atf_check -o inline:'100600\n120700\n' stat -f '%p' foo bar
	atf_check chmod -h 0500 bar
	atf_check -o inline:'100600\n120500\n' stat -f '%p' foo bar
	atf_check chmod 0660 bar
	atf_check -o inline:'100660\n120500\n' stat -f '%p' foo bar
}

atf_test_case v_flag
v_flag_head()
{
	atf_set	"descr" "Verify that setting a mode with -v emits the file when " \
			"doesn't emit an error message/exit with a non-zero " \
			"code"
}
v_flag_body()
{
	atf_check truncate -s 0 foo bar
	atf_check chmod 0600 foo
	atf_check chmod 0750 bar
	case "$(get_filesystem .)" in
	zfs)
		atf_expect_fail "ZFS updates mode for foo unnecessarily - bug 221188"
		;;
	esac
	atf_check -o 'inline:bar\n' chmod -v 0600 foo bar
	atf_check chmod -v 0600 foo bar
	for f in foo bar; do
		echo "$f: 0100600 [-rw------- ] -> 0100700 [-rwx------ ]";
	done > output.txt
	atf_check -o file:output.txt chmod -vv 0700 foo bar
	atf_check chmod -vv 0700 foo bar
}

atf_init_test_cases()
{
	atf_add_test_case RH_flag
	atf_add_test_case RL_flag
	atf_add_test_case RP_flag
	atf_add_test_case f_flag
	atf_add_test_case h_flag
	atf_add_test_case v_flag
}
