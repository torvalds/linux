#!/bin/bash
# SPDX-FileCopyrightText: 2023 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

d=$(dirname "${BASH_SOURCE[0]}")

parallel_log=$(mktemp -p /build)
basesha=$(git log -1 --pretty=%H .github/scripts/patches.sh)
patches=( $(git rev-list --reverse ${basesha}..HEAD) )

patch_tot=${#patches[@]}
num_commits=$((patch_tot + 3))

date -Iseconds
echo "Run PW tests"
echo "Top ${num_commits} commits"
git log -${num_commits} --abbrev=12 --pretty="commit %h (\"%s\")"

tm=$(mktemp -p /build)
rc=0
cnt=1

# Linear
for i in "${patches[@]}"; do
    \time --quiet -o $tm -f "took %es" \
	  bash ${d}/patches/patch_tester.sh ${i} ${cnt} ${patch_tot} || rc=1
    echo "::notice::Patch ${cnt}/${patch_tot} $(cat $tm)"
    cnt=$(( cnt + 1 ))
done
rm $tm
exit 0

# Parallel... (slower?)
parallel -j 4 --joblog ${parallel_log} --colsep=, bash ${d}/patches/patch_tester.sh \
	 {1} {2} {3} :::: <(
    for i in "${patches[@]}"; do
        echo ${i},${cnt},${patch_tot}
        cnt=$(( cnt + 1 ))
    done) || rc=1
cat ${parallel_log}

