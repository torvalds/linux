#!/bin/bash
# SPDX-FileCopyrightText: 2023 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

d=$(dirname "${BASH_SOURCE[0]}")
. $d/utils.sh
. $d/kselftest_prep.sh

rc=0
${d}/kernel_builder.sh rv64 kselftest plain gcc || rc=1
${d}/selftest_builder.sh rv64 kselftest plain gcc || rc=1
exit $rc
