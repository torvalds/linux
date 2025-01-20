#!/bin/bash
# SPDX-FileCopyrightText: 2025 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euox pipefail
d=$(dirname "${BASH_SOURCE[0]}")
. $d/series/utils.sh

logs=$(get_logs_dir)
f=${logs}/xfstests.log

KERNEL_PATH=$(find "$1" -name '*vmlinuz*')
mv $KERNEL_PATH $KERNEL_PATH.gz
gunzip  $KERNEL_PATH.gz

build_name=$(cat "$1/kernel_version")

# The Docker image comes with a prebuilt python environment with all tuxrun
# dependencies
source /build/.env/bin/activate

mkdir -p /build/squad_json/

/build/tuxrun/run --runtime null --device qemu-riscv64 --kernel $KERNEL_PATH --tests xfstests-xfs --results /build/squad_json/xfstests.json --log-file-text /build/squad_json/xfstests.log --timeouts xfstests=480  --overlay /build/xfstests.tar.xz || true

# Convert JSON to squad datamodel
python3 /build/my-linux/.github/scripts/series/tuxrun_to_squad_json.py --result-path /build/squad_json/xfstests.json --testsuite xfstests
python3 /build/my-linux/.github/scripts/series/generate_metadata.py --logs-path /build/squad_json/ --job-url ${GITHUB_JOB_URL} --branch ${GITHUB_BRANCH_NAME}

curl --header "Authorization: token $SQUAD_TOKEN" --form tests=@/build/squad_json/xfstests.squad.json --form log=@/build/squad_json/xfstests.log --form metadata=@/build/squad_json/metadata.json https://mazarinen.tail1c623.ts.net/api/submit/riscv-linux/linux-all/${build_name}/qemu
