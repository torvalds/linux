#!/bin/bash
# SPDX-FileCopyrightText: 2025 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euox pipefail
d=$(dirname "${BASH_SOURCE[0]}")
. $d/series/utils.sh

logs=$(get_logs_dir)
f=${logs}/isolated-tests.log

KERNEL_PATH=$(find "$1" -name '*vmlinuz*')
mv $KERNEL_PATH $KERNEL_PATH.gz
gunzip  $KERNEL_PATH.gz

ROOTFS_PATH=$(find /rootfs/ -name 'rootfs_rv64_ubuntu*.ext4')
# Resize the fs
truncate -s +4G $ROOTFS_PATH
resize2fs $ROOTFS_PATH

build_name=$(cat "$1/kernel_version")

# The Docker image comes with a prebuilt python environment with all tuxrun
# dependencies
source /build/.env/bin/activate

isolated_tests=( "cfi" )

mkdir -p /build/squad_json/
parallel_log=$(mktemp -p ${ci_root})

for test in ${isolated_tests[@]}; do
    /build/tuxrun/run --runtime null --device qemu-riscv64 --kernel $KERNEL_PATH --tests ${test} --results /build/squad_json/${test}.json --log-file-text /build/squad_json/${test}.log --timeouts ${test}=480  --overlay /build/isolated-${test}.tar.xz --rootfs $ROOTFS_PATH --boot-args "rw" || true
    # Convert JSON to squad datamodel
    python3 /build/my-linux/.github/scripts/series/tuxrun_to_squad_json.py --result-path /build/squad_json/${test}.json --testsuite ${test}
    python3 /build/my-linux/.github/scripts/series/generate_metadata.py --logs-path /build/squad_json/ --job-url ${GITHUB_JOB_URL} --branch ${GITHUB_BRANCH_NAME}

    curl --header "Authorization: token $SQUAD_TOKEN" --form tests=@/build/squad_json/${test}.squad.json --form log=@/build/squad_json/${test}.log --form metadata=@/build/squad_json/metadata.json https://mazarinen.tail1c623.ts.net/api/submit/riscv-linux/linux-all/${build_name}/qemu
done
