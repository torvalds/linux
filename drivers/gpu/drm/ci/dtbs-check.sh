#!/bin/bash
# SPDX-License-Identifier: MIT

set -euxo pipefail

: "${KERNEL_ARCH:?ERROR: KERNEL_ARCH must be set}"
: "${LLVM_VERSION:?ERROR: LLVM_VERSION must be set}"

./drivers/gpu/drm/ci/setup-llvm-links.sh

make LLVM=1 ARCH="${KERNEL_ARCH}" defconfig

if ! make -j"${FDO_CI_CONCURRENT:-4}" ARCH="${KERNEL_ARCH}" LLVM=1 dtbs_check \
        DT_SCHEMA_FILES="${SCHEMA:-}" 2>dtbs-check.log; then
    echo "ERROR: 'make dtbs_check' failed. Please check dtbs-check.log for details."
    exit 1
fi

if [[ -s dtbs-check.log ]]; then
    echo "WARNING: dtbs_check reported warnings. Please check dtbs-check.log for details."
    exit 102
fi
