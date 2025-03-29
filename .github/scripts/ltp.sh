#!/bin/bash
# SPDX-FileCopyrightText: 2023 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euox pipefail
d=$(dirname "${BASH_SOURCE[0]}")
. $d/series/utils.sh

logs=$(get_logs_dir)
f=${logs}/ltp.log

KERNEL_PATH=$(find "$1" -name '*vmlinuz*')
mv $KERNEL_PATH $KERNEL_PATH.gz
gunzip  $KERNEL_PATH.gz

build_name=$(cat "$1/kernel_version")

# The Docker image comes with a prebuilt python environment with all tuxrun
# dependencies
source /build/.env/bin/activate

# TODO ltp-controllers is too slow for now because of cgroup_fj_stress.sh
# but I haven't found an easy to skip this one from tuxrun
ltp_tests=( "ltp-commands"  "ltp-syscalls" "ltp-mm" "ltp-hugetlb" "ltp-crypto" "ltp-cve" "ltp-containers" "ltp-fs" "ltp-sched" )

mkdir -p /build/squad_json/
parallel_log=$(mktemp -p ${ci_root})

for ltp_test in ${ltp_tests[@]}; do
    echo "/build/tuxrun/run --runtime null --device qemu-riscv64 --kernel $KERNEL_PATH --tests $ltp_test --results /build/squad_json/$ltp_test.json --log-file-text /build/squad_json/$ltp_test.log --timeouts $ltp_test=480 || true"
done | parallel -j $(($(nproc)/4)) --colsep ' ' --joblog ${parallel_log}

cat ${parallel_log}
rm ${parallel_log}

for ltp_test in ${ltp_tests[@]}; do
    # Convert JSON to squad datamodel
    python3 /build/my-linux/.github/scripts/series/tuxrun_to_squad_json.py --result-path /build/squad_json/$ltp_test.json --testsuite $ltp_test
    python3 /build/my-linux/.github/scripts/series/generate_metadata.py --logs-path /build/squad_json/ --job-url ${GITHUB_JOB_URL} --branch ${GITHUB_BRANCH_NAME}

    curl --header "Authorization: token $SQUAD_TOKEN" --form tests=@/build/squad_json/$ltp_test.squad.json --form log=@/build/squad_json/$ltp_test.log --form metadata=@/build/squad_json/metadata.json https://mazarinen.tail1c623.ts.net/api/submit/riscv-linux/linux-all/${build_name}/qemu
done
