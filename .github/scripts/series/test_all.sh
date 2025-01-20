#!/bin/bash
# SPDX-FileCopyrightText: 2023 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

d=$(dirname "${BASH_SOURCE[0]}")
. $d/utils.sh

$d/unpack_fw.sh

parallel_log=$(mktemp -p ${ci_root})
parallel -j $(($(nproc)/4)) --colsep ' ' --joblog ${parallel_log} \
         ${d}/kernel_tester.sh {1} {2} {3} {4} {5} :::: <($d/generate_test_runs.sh) || true
cat ${parallel_log}
rm ${parallel_log}
