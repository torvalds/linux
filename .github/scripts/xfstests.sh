#!/bin/bash
# SPDX-FileCopyrightText: 2025 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euox pipefail
d=$(dirname "${BASH_SOURCE[0]}")
. $d/series/utils.sh

logs=$(get_logs_dir)
f=${logs}/xfstests.log

KERNEL_PATH=$(find "$1" -name '*vmlinu[zx]*')
mv $KERNEL_PATH $KERNEL_PATH.gz
gunzip  $KERNEL_PATH.gz

ROOTFS_PATH=$(find /rootfs/ -name 'rootfs_rv64_ubuntu*.ext4')
# Resize the fs
truncate -s +20G $ROOTFS_PATH
resize2fs $ROOTFS_PATH

build_name=$(cat "$1/kernel_version")

# The Docker image comes with a prebuilt python environment with all tuxrun
# dependencies
source /build/.env/bin/activate

xfs_tests=( "xfstests-ext4" "xfstests-btrfs" "xfstests-f2fs" "xfstests-xfs" )

mkdir -p /build/squad_json/
parallel_log=$(mktemp -p ${ci_root})

for xfs_test in ${xfs_tests[@]}; do
    echo "/build/tuxrun/run --runtime null --device qemu-riscv64 --kernel $KERNEL_PATH --tests ${xfs_test} --results /build/squad_json/${xfs_test}.json --log-file-text /build/squad_json/${xfs_test}.log --timeouts ${xfs_test}=480  --overlay /build/xfstests.tar.xz --rootfs $ROOTFS_PATH --boot-args \"rw\" || true"
done | parallel -j $(($(nproc)/4)) --colsep ' ' --joblog ${parallel_log}

cat ${parallel_log}
rm ${parallel_log}

for xfs_test in ${xfs_tests[@]}; do
    # Convert JSON to squad datamodel
    python3 /build/my-linux/.github/scripts/series/tuxrun_to_squad_json.py --result-path /build/squad_json/${xfs_test}.json --testsuite ${xfs_test}
    python3 /build/my-linux/.github/scripts/series/generate_metadata.py --logs-path /build/squad_json/ --job-url ${GITHUB_JOB_URL} --branch ${GITHUB_BRANCH_NAME}

    curl --header "Authorization: token $SQUAD_TOKEN" --form tests=@/build/squad_json/${xfs_test}.squad.json --form log=@/build/squad_json/${xfs_test}.log --form metadata=@/build/squad_json/metadata.json https://mazarinen.tail1c623.ts.net/api/submit/riscv-linux/linux-all/${build_name}/qemu
done
