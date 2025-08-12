#!/bin/bash
# SPDX-License-Identifier: MIT

set -euxo pipefail

: "${KERNEL_ARCH:?ERROR: KERNEL_ARCH must be set}"
: "${LLVM_VERSION:?ERROR: LLVM_VERSION must be set}"

./drivers/gpu/drm/ci/setup-llvm-links.sh

export PATH="/usr/bin:$PATH"

./tools/testing/kunit/kunit.py run \
  --arch "${KERNEL_ARCH}" \
  --make_options LLVM=1 \
  --kunitconfig=drivers/gpu/drm/tests
