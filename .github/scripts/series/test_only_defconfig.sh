#!/bin/bash
# SPDX-FileCopyrightText: 2024 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

d=$(dirname "${BASH_SOURCE[0]}")
. $d/utils.sh

$d/unpack_fw.sh

rc=0
${d}/kernel_tester.sh rv64 defconfig plain gcc ubuntu || rc=1
exit $rc
