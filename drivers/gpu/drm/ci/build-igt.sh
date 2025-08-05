#!/bin/bash
# SPDX-License-Identifier: MIT

set -ex

function generate_testlist {
    set +x
    while read -r line; do
        if [ "$line" = "TESTLIST" ] || [ "$line" = "END TESTLIST" ]; then
            continue
        fi

        tests=$(echo "$line" | tr ' ' '\n')

        for test in $tests; do
            output=$(/igt/libexec/igt-gpu-tools/"$test" --list-subtests || true)

            if [ -z "$output" ]; then
                echo "$test"
            else
                echo "$output" | while read -r subtest; do
                    echo "$test@$subtest"
                done
            fi
        done
    done < /igt/libexec/igt-gpu-tools/test-list.txt > /igt/libexec/igt-gpu-tools/ci-testlist.txt
    set -x
}

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

if [[ "$KERNEL_ARCH" = "arm64" ]] || [[ "$KERNEL_ARCH" = "arm" ]]; then
    MESON_OPTIONS="$MESON_OPTIONS -Dxe_driver=disabled"
fi

mkdir -p /igt
meson build $MESON_OPTIONS $EXTRA_MESON_ARGS
ninja -C build -j${FDO_CI_CONCURRENT:-4} || ninja -C build -j 1
ninja -C build install

if [[ "$KERNEL_ARCH" = "arm64" ]]; then
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/igt/lib/aarch64-linux-gnu
elif [[ "$KERNEL_ARCH" = "arm" ]]; then
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/igt/lib
else
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/igt/lib64
fi

echo "Generating ci-testlist.txt"
generate_testlist

mkdir -p artifacts/
tar -cf artifacts/igt.tar /igt

# Pass needed files to the test stage
S3_ARTIFACT_NAME="igt.tar.gz"
gzip -c artifacts/igt.tar > ${S3_ARTIFACT_NAME}
ci-fairy s3cp --token-file "${S3_JWT_FILE}" ${S3_ARTIFACT_NAME} https://${PIPELINE_ARTIFACTS_BASE}/${KERNEL_ARCH}/${S3_ARTIFACT_NAME}
