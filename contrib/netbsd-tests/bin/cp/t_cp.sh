# $NetBSD: t_cp.sh,v 1.1 2012/03/17 16:33:10 jruoho Exp $
#
# Copyright (c) 2007, 2008 The NetBSD Foundation, Inc.
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

FILES="file file2 file3 link dir dir2 dirlink target"

cleanup() {
        rm -fr ${FILES}
}

cp_compare() {
	echo "Ensuring that $2 and $3 are identical"
	cmp -s $2 $3 || atf_fail "$2 and $3 are different"
}

reset() {
	cleanup
	echo "I'm a file" > file
	echo "I'm a file, 2" > file2
	echo "I'm a file, 3" > file3
	ln -s file link
	mkdir dir
	ln -s dir dirlink
}

atf_test_case file_to_file
file_to_file_head() {
	atf_set "descr" "Checks the copy of a file to a file"
}
file_to_file_body() {
	reset

	file_to_file_simple
	file_to_file_preserve
	file_to_file_noflags
}

file_to_file_simple() {
	rm -f file2
	umask 022
	chmod 777 file
	atf_check -s eq:0 -o empty -e empty cp file file2
	cp_compare file_to_file_simple file file2
	if [ `stat -f "%Lp" file2` != "755" ]; then
		atf_fail "new file not created with umask"
	fi

	chmod 644 file
	chmod 777 file2
	cp_compare file_to_file_simple file file2
	if [ `stat -f "%Lp" file2` != "777" ]; then
		atf_fail "existing files permissions not retained"
	fi
}

file_to_file_preserve() {
	rm file3
	chmod 644 file
	chflags nodump file
	atf_check -s eq:0 -o empty -e empty cp -p file file3
	finfo=`stat -f "%p%u%g%m%z%f" file`
	f3info=`stat -f "%p%u%g%m%z%f" file3`
	if [ $finfo != $f3info ]; then
		atf_fail "attributes not preserved"
	fi
}

file_to_file_noflags() {
	rm file3
	chmod 644 file
	chflags nodump file
	atf_check -s eq:0 -o empty -e empty cp -p -N file file3
	finfo=`stat -f "%f" file`
	f3info=`stat -f "%f" file3`
	if [ $finfo = $f3info ]; then
		atf_fail "-p -N preserved file flags"
	fi
}

atf_test_case file_to_link
file_to_link_head() {
	atf_set "descr" "Checks the copy of a file to a symbolic link"
}
file_to_link_body() {
	reset
	atf_check -s eq:0 -o empty -e empty cp file2 link
	cp_compare file_to_link file file2
}

atf_test_case link_to_file
link_to_file_head() {
	atf_set "descr" "Checks the copy of a symbolic link to a file"
}
link_to_file_body() {
	reset
	# file and link are identical (not copied).
	atf_check -s eq:1 -o empty -e ignore cp link file
	atf_check -s eq:0 -o empty -e empty cp link file2
	cp_compare link_to_file file file2
}

atf_test_case file_over_link
file_over_link_head() {
	atf_set "descr" "Checks the copy of a file to a symbolic link" \
	                "without following it"
}
file_over_link_body() {
	reset
	atf_check -s eq:0 -o empty -e empty cp -P file link
	cp_compare file_over_link file link
}

atf_test_case link_over_file
link_over_file_head() {
	atf_set "descr" "Checks the copy of a symbolic link to a file" \
	                "without following the former"
}
link_over_file_body() {
	reset
	atf_check -s eq:0 -o empty -e empty cp -P link file
	if [ `readlink link` != `readlink file` ]; then
		atf_fail "readlink link != readlink file"
	fi
}

atf_test_case files_to_dir
files_to_dir_head() {
	atf_set "descr" "Checks the copy of multiple files into a directory"
}
files_to_dir_body() {
	reset
	# can't copy multiple files to a file
	atf_check -s eq:1 -o empty -e ignore cp file file2 file3
	atf_check -s eq:0 -o empty -e empty cp file file2 link dir
	cp_compare files_to_dir file "dir/file"
}

atf_test_case dir_to_file
dir_to_file_head() {
	atf_set "descr" "Checks the copy of a directory onto a file, which" \
	                "should not work"
}
dir_to_file_body() {
	reset
	# can't copy a dir onto a file
	atf_check -s eq:1 -o empty -e ignore cp dir file
	atf_check -s eq:1 -o empty -e ignore cp -R dir file
}

atf_test_case file_to_linkdir
file_to_linkdir_head() {
	atf_set "descr" "Checks the copy of a file to a symbolic link that" \
	                "points to a directory"
}
file_to_linkdir_body() {
	reset
	atf_check -s eq:0 -o empty -e empty cp file dirlink
	cp_compare file_to_linkdir file "dir/file"

	# overwrite the link
	atf_check -s eq:0 -o empty -e empty cp -P file dirlink
	atf_check -s eq:1 -o empty -e empty readlink dirlink
	cp_compare file_to_linkdir file dirlink
}

atf_test_case linkdir_to_file
linkdir_to_file_head() {
	atf_set "descr" "Checks the copy of a symbolic link that points to" \
	                "a directory onto a file"
}
linkdir_to_file_body() {
	reset
	# cannot copy a dir onto a file
	atf_check -s eq:1 -o empty -e ignore cp dirlink file

	# overwrite the link
	atf_check -s eq:0 -o empty -e empty cp -P dirlink file
	if [ `readlink file` != `readlink dirlink` ]; then
		atf_fail "readlink link != readlink file"
	fi
}

dir_to_dne_no_R() {
	atf_check -s eq:1 -o empty -e ignore cp dir dir2
}

dir_to_dne() {
	atf_check -s eq:0 -o empty -e empty cp -R dir dir2
	cp_compare dir_to_dne "dir/file" "dir2/file"
	readlink dir2/link >/dev/null
	if [ $? -gt 0 ]; then
		atf_fail "-R didn't copy a link as a link"
	fi
}

dir_to_dir_H() {
	dir_to_dir_setup
	atf_check -s eq:0 -o empty -e empty cp -R dir dir2

	chmod 777 dir

	# copy a dir into a dir, only command-line links are followed
	atf_check -s eq:0 -o empty -e empty cp -R -H dirlink dir2
	cp_compare dir_to_dir_H "dir/file" "dir2/dirlink/file"
	readlink dir2/dirlink/link >/dev/null
	if [ $? -gt 0 ]; then
		atf_fail "didn't copy a link as a link"
	fi

	# Created directories have the same mode as the corresponding
        # source directory, unmodified by the process's umask.
	if [ `stat -f "%Lp" dir2/dirlink` != "777" ]; then
		atf_fail "-R modified dir perms with umask"
	fi
}

dir_to_dir_L() {
	dir_to_dir_setup
	atf_check -s eq:0 -o empty -e empty cp -R dir dir2
	atf_check -s eq:0 -o empty -e empty cp -R -H dirlink dir2

	# copy a dir into a dir, following all links
	atf_check -s eq:0 -o empty -e empty cp -R -H -L dirlink dir2/dirlink
	cp_compare dir_to_dir_L "dir/file" "dir2/dirlink/dirlink/file"
	# fail if -R -L copied a link as a link
	atf_check -s eq:1 -o ignore -e empty readlink dir2/dirlink/dirlink/link
}

dir_to_dir_subdir_exists() {
	# recursively copy a dir into another dir, with some subdirs already
	# existing
	cleanup

	mkdir -p dir/1 dir/2 dir/3 target/2
	echo "file" > dir/2/file
	atf_check -s eq:0 -o empty -e empty cp -R dir/* target
	cp_compare dir_to_dir_subdir_exists "dir/2/file" "target/2/file"
}

dir_to_dir_setup() {
	reset
	umask 077
	cp -P file file2 file3 link dir
}

atf_test_case dir_to_dir
dir_to_dir_head() {
	atf_set "descr" "Checks the copy of a directory onto another directory"
}
dir_to_dir_body() {
	dir_to_dir_setup
	dir_to_dne_no_R
	dir_to_dne
	dir_to_dir_H
	dir_to_dir_L
	dir_to_dir_subdir_exists
}

atf_init_test_cases()
{
	atf_add_test_case file_to_file
	atf_add_test_case file_to_link
	atf_add_test_case link_to_file
	atf_add_test_case file_over_link
	atf_add_test_case link_over_file
	atf_add_test_case files_to_dir
	atf_add_test_case file_to_linkdir
	atf_add_test_case linkdir_to_file
	atf_add_test_case dir_to_file
	atf_add_test_case dir_to_dir
}
