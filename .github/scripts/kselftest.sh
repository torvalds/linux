#!/bin/bash
# SPDX-FileCopyrightText: 2023 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail
d=$(dirname "${BASH_SOURCE[0]}")
. $d/series/utils.sh

logs=$(get_logs_dir)
f=${logs}/kselftest.log

date -Iseconds | tee -a ${f}
echo "Build, boot, and run kselftests on various kernels" | tee -a ${f}
echo "Top 16 commits" | tee -a ${f}
git log -16 --abbrev=12 --pretty="commit %h (\"%s\")" | tee -a ${f}

kernel_base_sha=$(git log -1 --pretty=%H $(git log -1 --reverse --pretty=%H .github)^)
echo "build_name $(git describe --tags ${kernel_base_sha})" | tee -a ${f}

${d}/series/build_only_kselftest.sh | tee -a ${f}
${d}/series/test_only_kselftest.sh | tee -a ${f}
