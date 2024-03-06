#!/bin/bash
# SPDX-License-Identifier: MIT

set -ex

git clone https://gitlab.freedesktop.org/drm/igt-gpu-tools.git --single-branch --no-checkout
cd igt-gpu-tools
git checkout $IGT_VERSION

if [[ "$KERNEL_ARCH" = "arm" ]]; then
    . ../.gitlab-ci/container/create-cross-file.sh armhf
    EXTRA_MESON_ARGS="--cross-file /cross_file-armhf.txt"
fi

MESON_OPTIONS="-Doverlay=disabled                    \
               -Dchamelium=disabled                  \
               -Dvalgrind=disabled                   \
               -Dman=enabled                         \
               -Dtests=enabled                       \
               -Drunner=enabled                      \
               -Dlibunwind=enabled                   \
               -Dprefix=/igt"

mkdir -p /igt
meson build $MESON_OPTIONS $EXTRA_MESON_ARGS
ninja -C build -j${FDO_CI_CONCURRENT:-4} || ninja -C build -j 1
ninja -C build install

mkdir -p artifacts/
tar -cf artifacts/igt.tar /igt

# Pass needed files to the test stage
S3_ARTIFACT_NAME="igt.tar.gz"
gzip -c artifacts/igt.tar > ${S3_ARTIFACT_NAME}
ci-fairy s3cp --token-file "${CI_JOB_JWT_FILE}" ${S3_ARTIFACT_NAME} https://${PIPELINE_ARTIFACTS_BASE}/${KERNEL_ARCH}/${S3_ARTIFACT_NAME}
