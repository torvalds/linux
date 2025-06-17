#!/bin/bash
# SPDX-FileCopyrightText: 2023 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

d=$(dirname "${BASH_SOURCE[0]}")
. $d/utils.sh
. $d/kselftest_prep.sh

$d/unpack_fw.sh
rc=0

logs=$(get_logs_dir)
subtests=${logs}/kselftest-collections.txt
readarray -t kselftest_subtests < ${subtests}

parallel_log=$(mktemp -p ${ci_root})

for subtest in "${kselftest_subtests[@]}"; do
    echo "${d}/kernel_tester.sh rv64 ${subtest} plain gcc ubuntu"
done | parallel -j$(($(nproc)/8)) --colsep ' ' --joblog ${parallel_log} || true

cat ${parallel_log}
rm ${parallel_log}
