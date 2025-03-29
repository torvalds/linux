#!/bin/bash
# SPDX-FileCopyrightText: 2023 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

d=$(dirname "${BASH_SOURCE[0]}")
. $d/utils.sh

rc=0
while read xlen config fragment toolchain; do
    ${d}/kernel_builder.sh $xlen $config $fragment $toolchain || rc=1
    if [[ $config == "kselftest" ]]; then
	${d}/selftest_builder.sh $xlen $config $fragment $toolchain || rc=1
    fi
done < <($d/generate_build_configs.sh)
exit 0
