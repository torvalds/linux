#!/bin/sh
#
# $NetBSD: h_funcs.subr,v 1.5 2013/03/17 01:16:45 jmmv Exp $
#
# Copyright (c) 2005, 2006, 2007 The NetBSD Foundation, Inc.
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

Mount_Point=

#
# test_mount [args]
#
#	Mounts tmpfs over ${Mount_Point} and changes the current directory
#	to the mount point.  Optional arguments may be passed to the
#	mount command.
#
test_mount() {
	require_fs tmpfs

	Mount_Point=$(pwd)/mntpt
	atf_check -s eq:0 -o empty -e empty mkdir "${Mount_Point}"
	echo "mount -t tmpfs ${*} tmpfs ${Mount_Point}"
	mount -t tmpfs "${@}" tmpfs "${Mount_Point}" 2>mounterr
	if [ "${?}" -ne 0 ]; then
		cat mounterr 1>&2
		if grep 'Operation not supported' mounterr > /dev/null; then
			atf_skip "tmpfs not supported"
		fi 
		atf_fail "Failed to mount a tmpfs file system"
	fi
	cd "${Mount_Point}"
}

#
# test_unmount
#
#	Unmounts the file system mounted by test_mount.
#
test_unmount() {
	# Begin FreeBSD
	_test_unmount
	exit_code=$?
	atf_check_equal "$exit_code" "0"
	return $exit_code
	# End FreeBSD
	cd - >/dev/null
	atf_check -s eq:0 -o empty -e empty umount ${Mount_Point}
	atf_check -s eq:0 -o empty -e empty rmdir ${Mount_Point}
	Mount_Point=
}

# Begin FreeBSD
_test_unmount() {
	if [ -z "${Mount_Point}" -o ! -d "${Mount_Point}" ]; then
		return 0
	fi

	cd - >/dev/null
	umount ${Mount_Point}
	rmdir ${Mount_Point}
	Mount_Point=
}
# End FreeBSD

#
# kqueue_monitor expected_nevents file1 [.. fileN]
#
#	Monitors the commands given through stdin (one per line) using
#	kqueue and stores the events raised in a log that can be later
#	verified with kqueue_check.
#
kqueue_monitor() {
	nev=${1}; shift
	echo "Running kqueue-monitored commands and expecting" \
	    "${nev} events"
	$(atf_get_srcdir)/h_tools kqueue ${*} >kqueue.log || \
	    atf_fail "Could not launch kqueue monitor"
	got=$(wc -l kqueue.log | awk '{ print $1 }')
	test ${got} -eq ${nev} || \
	    atf_fail "Got ${got} events but expected ${nev}"
}

#
# kqueue_check file event
#
#	Checks if kqueue raised the given event when monitoring the
#	given file.
#
kqueue_check() {
	echo "Checking if ${1} received ${2}"
	grep "^${1} - ${2}$" kqueue.log >/dev/null || \
	    atf_fail "${1} did not receive ${2}"
}
