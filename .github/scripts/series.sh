#!/bin/bash
# SPDX-FileCopyrightText: 2023 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail
d=$(dirname "${BASH_SOURCE[0]}")
. $d/series/utils.sh


logs=$(get_logs_dir)
f=${logs}/series.log

date -Iseconds | tee -a ${f}
echo "Build, and boot various kernels" | tee -a ${f}
echo "Top 16 commits" | tee -a ${f}
git log -16 --abbrev=12 --pretty="commit %h (\"%s\")" | tee -a ${f}

kernel_base_sha=$(git log -1 --pretty=%H $(git log -1 --reverse --pretty=%H .github)^)
echo "build_name $(git describe --tags ${kernel_base_sha})" | tee -a ${f}
build_name=$(git describe --tags ${kernel_base_sha})

${d}/series/build_all.sh | tee -a ${f}
${d}/series/test_all.sh | tee -a ${f}

# Some logs contain invalid bytes (not utf-8) and then makes the following
# script fail so convert them all.
for f in `ls ${logs}`; do
    iconv -c -t utf-8 ${logs}/${f} > ${logs}/${f}_tmp
    mv ${logs}/${f}_tmp ${logs}/${f}
done

python3 ${d}/series/github_ci_squad_results.py --logs-path ${logs}
python3 ${d}/series/generate_metadata.py --logs-path ${logs} \
	--job-url ${GITHUB_JOB_URL} --branch ${GITHUB_BRANCH_NAME}

curl --header "Authorization: token ${SQUAD_TOKEN}" \
     --form tests=@${logs}/squad.json \
     --form metadata=@${logs}/metadata.json \
     https://mazarinen.tail1c623.ts.net/api/submit/riscv-linux/linux-all/${build_name}/qemu
