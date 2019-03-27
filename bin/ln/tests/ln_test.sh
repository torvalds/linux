#
# Copyright 2017 Shivansh Rai
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
#

set_umask()
{
	if ! umask 022; then
		atf_fail "setting umask failed"
	fi
}

atf_test_case L_flag
L_flag_head()
{
	atf_set "descr" "Verify that when creating a hard link to a " \
			"symbolic link, '-L' option creates a hard" \
			"link to the target of the symbolic link"
}

L_flag_body()
{
	set_umask
	atf_check touch A
	atf_check ln -s A B
	atf_check ln -L B C
	stat_A=$(stat -f %i A)
	stat_C=$(stat -f %i C)
	atf_check_equal "$stat_A" "$stat_C"
	atf_check -o inline:'Symbolic Link\n' stat -f %SHT B
	atf_check -o inline:'A\n' readlink B
}

atf_test_case P_flag
P_flag_head()
{
	atf_set "descr" "Verify that when creating a hard link to a " \
			"symbolic link, '-P' option creates a hard " \
			"link to the symbolic link itself"
}

P_flag_body()
{
	set_umask
	atf_check touch A
	atf_check ln -s A B
	atf_check ln -P B C
	stat_B=$(stat -f %i B)
	stat_C=$(stat -f %i C)
	atf_check_equal "$stat_B" "$stat_C"
}

atf_test_case f_flag
f_flag_head()
{
	atf_set "descr" "Verify that if the target file already exists, " \
			"'-f' option unlinks it so that link may occur"
}

f_flag_body()
{
	set_umask
	atf_check touch A B
	atf_check ln -f A B
	stat_A=$(stat -f %i A)
	stat_B=$(stat -f %i B)
	atf_check_equal "$stat_A" "$stat_B"
}

atf_test_case target_exists_hard
target_exists_hard_head()
{
	atf_set "descr" "Verify whether creating a hard link fails if the " \
			"target file already exists"
}

target_exists_hard_body()
{
	set_umask
	atf_check touch A B
	atf_check -s exit:1 -e inline:'ln: B: File exists\n' \
		ln A B
}

atf_test_case target_exists_symbolic
target_exists_symbolic_head()
{
	atf_set "descr" "Verify whether creating a symbolic link fails if " \
			"the target file already exists"
}

target_exists_symbolic_body()
{
	set_umask
	atf_check touch A B
	atf_check -s exit:1 -e inline:'ln: B: File exists\n' \
		ln -s A B
}

atf_test_case shf_flag_dir
shf_flag_dir_head() {
	atf_set "descr" "Verify that if the target directory is a symbolic " \
			"link, '-shf' option prevents following the link"
}

shf_flag_dir_body()
{
	atf_check mkdir -m 0777 A B
	atf_check ln -s A C
	atf_check ln -shf B C
	atf_check -o inline:'Symbolic Link\n' stat -f %SHT C
	atf_check -o inline:'B\n' readlink C
}

atf_test_case snf_flag_dir
snf_flag_dir_head() {
	atf_set "descr" "Verify that if the target directory is a symbolic " \
			"link, '-snf' option prevents following the link"
}

snf_flag_dir_body()
{
	atf_check mkdir -m 0777 A B
	atf_check ln -s A C
	atf_check ln -snf B C
	atf_check -o inline:'Symbolic Link\n' stat -f %SHT C
	atf_check -o inline:'B\n' readlink C
}

atf_test_case sF_flag
sF_flag_head()
{
	atf_set "descr" "Verify that if the target file already exists " \
			"and is a directory, then '-sF' option removes " \
			"it so that the link may occur"
}

sF_flag_body()
{
	atf_check mkdir A B
	atf_check ln -sF A B
	atf_check -o inline:'Symbolic Link\n' stat -f %SHT B
	atf_check -o inline:'A\n' readlink B
}

atf_test_case sf_flag
sf_flag_head()
{
	atf_set "descr" "Verify that if the target file already exists, " \
			"'-sf' option unlinks it and creates a symbolic link " \
			"to the source file"
}

sf_flag_body()
{
	set_umask
	atf_check touch A B
	atf_check ln -sf A B
	atf_check -o inline:'Symbolic Link\n' stat -f %SHT B
	atf_check -o inline:'A\n' readlink B
}

atf_test_case s_flag
s_flag_head()
{
	atf_set "descr" "Verify that '-s' option creates a symbolic link"
}

s_flag_body()
{
	set_umask
	atf_check touch A
	atf_check ln -s A B
	atf_check -o inline:'Symbolic Link\n' stat -f %SHT B
	atf_check -o inline:'A\n' readlink B
}

atf_test_case s_flag_broken
s_flag_broken_head()
{
	atf_set "descr" "Verify that if the source file does not exists, '-s' " \
			"option creates a broken symbolic link to the source file"
}

s_flag_broken_body()
{
	atf_check ln -s A B
	atf_check -o inline:'Symbolic Link\n' stat -f %SHT B
	atf_check -o inline:'A\n' readlink B
}

atf_test_case sw_flag
sw_flag_head()
{
	atf_set "descr" "Verify that '-sw' option produces a warning if the " \
			"source of a symbolic link does not currently exist"
}

sw_flag_body()
{
	atf_check -s exit:0 -e inline:'ln: warning: A: No such file or directory\n' \
		ln -sw A B
	atf_check -o inline:'Symbolic Link\n' stat -f %SHT B
	atf_check -o inline:'A\n' readlink B
}

atf_init_test_cases()
{
	atf_add_test_case L_flag
	atf_add_test_case P_flag
	atf_add_test_case f_flag
	atf_add_test_case target_exists_hard
	atf_add_test_case target_exists_symbolic
	atf_add_test_case shf_flag_dir
	atf_add_test_case snf_flag_dir
	atf_add_test_case sF_flag
	atf_add_test_case sf_flag
	atf_add_test_case s_flag
	atf_add_test_case s_flag_broken
	atf_add_test_case sw_flag
}
