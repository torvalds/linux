#!/bin/bash
# SPDX-FileCopyrightText: 2023 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

d=$(dirname "${BASH_SOURCE[0]}")

sha1=$1
patch_num=$2
patch_tot=$3

tm=$(mktemp -p /build)

worktree=$(mktemp -d -p /build)
git worktree add $worktree ${sha1} &>/dev/null
cd $worktree

rc=0
tests=( $(ls ${d}/tests/*.sh) )
tcnt=1
for j in "${tests[@]}"; do
    git reset --hard ${sha1} &>/dev/null
    msg="Patch ${patch_num}/${patch_tot}: Test ${tcnt}/${#tests[@]}: ${j}"
    echo "::group::${msg}"
    testrc=0
    \time --quiet -o $tm -f "took %es" \
          bash ${j} || testrc=$?
    echo "::endgroup::"
    if (( $testrc == 250 )); then
        rc=1
        echo "::warning::WARN ${msg} $(cat $tm)"
    elif (( $testrc )); then
        rc=1
        echo "::error::FAIL ${msg} $(cat $tm)"
    else
        echo "::notice::OK ${msg} $(cat $tm)"
    fi
    tcnt=$(( tcnt + 1 ))
done

git worktree remove $worktree &>/dev/null || true
rm $tm
exit $rc
