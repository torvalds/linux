#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2019 Stephen Rothwell <sfr@canb.auug.org.au>
# Copyright (C) 2019 Greg Kroah-Hartman <gregkh@linuxfoundation.org>
#
# Verify that the signed-off-by chain looks correct for a range of git commits.
#
# usage:
#	verify_signedoff.sh GIT_RANGE
#
# To test just the HEAD commit do:
#	verify_signedoff.sh HEAD^..HEAD
#
#
# Thanks to Stephen Rothwell <sfr@canb.auug.org.au> for the majority of this code
#

help()
{
	echo "error, git range not found"
	echo "usage:"
	echo "	$0 GIT_RANGE"
	exit 1
}

verify_signedoff()
{
	git_range=$1
	error=false
	for c in $(git rev-list --no-merges "${git_range}"); do
		ae=$(git log -1 --format='%ae' "$c")
		aE=$(git log -1 --format='%aE' "$c")
		an=$(git log -1 --format='%an' "$c")
		aN=$(git log -1 --format='%aN' "$c")
		ce=$(git log -1 --format='%ce' "$c")
		cE=$(git log -1 --format='%cE' "$c")
		cn=$(git log -1 --format='%cn' "$c")
		cN=$(git log -1 --format='%cN' "$c")
		sob=$(git log -1 --format='%b' "$c" | grep -i '^[[:space:]]*Signed-off-by:')

		am=false
		cm=false
		grep -i -q "<$ae>" <<<"$sob" ||
			grep -i -q "<$aE>" <<<"$sob" ||
			grep -i -q ":[[:space:]]*${an}[[:space:]]*<" <<<"$sob" ||
			grep -i -q ":[[:space:]]*${aN}[[:space:]]*<" <<<"$sob" ||
			am=true
		grep -i -q "<$ce>" <<<"$sob" ||
			grep -i -q "<$cE>" <<<"$sob" ||
			grep -i -q ":[[:space:]]*${cn}[[:space:]]*<" <<<"$sob" ||
			grep -i -q ":[[:space:]]*${cN}[[:space:]]*<" <<<"$sob" ||
			cm=true

		if "$am" || "$cm"; then
			printf "Commit %s\n" "$(git show -s --abbrev-commit --abbrev=12 --pretty=format:"%h (\"%s\")%n" "${c}")"
			"$am" && printf "\tauthor Signed-off-by missing\n"
			"$cm" && printf "\tcommitter Signed-off-by missing\n"
			printf "\tauthor email:    %s\n" "$ae"
			printf "\tcommitter email: %s\n"  "$ce"
			readarray -t s <<< "${sob}"
			printf "\t%s\n" "${s[@]}"
			printf "\n"
			error=true
		fi
	done
	if "$error"; then
		echo "Errors in tree with Signed-off-by, please fix!"
		exit 1
	fi
        echo "Signed-off-by tag matches author and committer"
}

git_range="HEAD~..HEAD"

if [ "${git_range}" == "" ] ; then
	help
fi

verify_signedoff "${git_range}"
exit 0
