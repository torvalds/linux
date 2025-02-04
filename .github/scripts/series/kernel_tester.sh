#!/bin/bash
# SPDX-FileCopyrightText: 2023 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

d=$(dirname "${BASH_SOURCE[0]}")
. $d/utils.sh
. $d/qemu_test_utils.sh

xlen=$1
config=$2
fragment=$3
toolchain=$4
rootfs=$5

generate_qemu_subtests $xlen $config $fragment $toolchain $rootfs

tm=$(mktemp -p ${ci_root})
n=$(gen_kernel_name $xlen $config $fragment $toolchain)
logs=$(get_logs_dir)
tot=${#qemu_subtests[@]}
allrc=0
for i in $(seq $tot); do
    rc=0
    tstn=$(get_qemu_test_name ${qemu_subtests[$(($i - 1))]})
    tst=${qemu_subtests[$(($i - 1))]}

    log="test_kernel___${n}___${rootfs}___${tstn}.log"
    \time --quiet -o $tm -f "took %es" \
	  $d/test_kernel.sh "${xlen}" "${config}" "${fragment}" "${toolchain}" "${rootfs}" \
	  $tst &> "${logs}/${log}" || rc=$?
    if (( $rc )); then
	allrc=1
	echo "::error::FAIL Test kernel ${n} ${rootfs} ${tst} $i/$tot \"${log}\" $(cat $tm)"
    else
	echo "::notice::OK Test kernel ${n} ${rootfs} ${tst} $i/$tot $(cat $tm)"
    fi
done
rm $tm
exit $allrc
