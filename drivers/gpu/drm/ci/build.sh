#!/bin/bash
# SPDX-License-Identifier: MIT

set -ex

# Clean up stale rebases that GitLab might not have removed when reusing a checkout dir
rm -rf .git/rebase-apply

. .gitlab-ci/container/container_pre_build.sh

# libssl-dev was uninstalled because it was considered an ephemeral package
apt-get update
apt-get install -y libssl-dev

if [[ "$KERNEL_ARCH" = "arm64" ]]; then
    GCC_ARCH="aarch64-linux-gnu"
    DEBIAN_ARCH="arm64"
    DEVICE_TREES="arch/arm64/boot/dts/rockchip/rk3399-gru-kevin.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/amlogic/meson-gxl-s805x-libretech-ac.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/allwinner/sun50i-h6-pine-h64.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/amlogic/meson-gxm-khadas-vim2.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/qcom/apq8016-sbc-usb-host.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/qcom/apq8096-db820c.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/amlogic/meson-g12b-a311d-khadas-vim3.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/mediatek/mt8173-elm-hana.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/mediatek/mt8183-kukui-jacuzzi-juniper-sku16.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/mediatek/mt8192-asurada-spherion-r0.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/qcom/sc7180-trogdor-lazor-limozeen-nots-r5.dtb"
elif [[ "$KERNEL_ARCH" = "arm" ]]; then
    GCC_ARCH="arm-linux-gnueabihf"
    DEBIAN_ARCH="armhf"
    DEVICE_TREES="arch/arm/boot/dts/rockchip/rk3288-veyron-jaq.dtb"
    DEVICE_TREES+=" arch/arm/boot/dts/allwinner/sun8i-h3-libretech-all-h3-cc.dtb"
    DEVICE_TREES+=" arch/arm/boot/dts/nxp/imx/imx6q-cubox-i.dtb"
    apt-get install -y libssl-dev:armhf
else
    GCC_ARCH="x86_64-linux-gnu"
    DEBIAN_ARCH="amd64"
    DEVICE_TREES=""
fi

export ARCH=${KERNEL_ARCH}
export CROSS_COMPILE="${GCC_ARCH}-"

# The kernel doesn't like the gold linker (or the old lld in our debians).
# Sneak in some override symlinks during kernel build until we can update
# debian.
mkdir -p ld-links
for i in /usr/bin/*-ld /usr/bin/ld; do
    i=$(basename $i)
    ln -sf /usr/bin/$i.bfd ld-links/$i
done

NEWPATH=$(pwd)/ld-links
export PATH=$NEWPATH:$PATH

git config --global user.email "fdo@example.com"
git config --global user.name "freedesktop.org CI"
git config --global pull.rebase true

# cleanup git state on the worker
rm -rf .git/rebase-merge

# Try to merge fixes from target repo
if [ "$(git ls-remote --exit-code --heads ${UPSTREAM_REPO} ${TARGET_BRANCH}-external-fixes)" ]; then
    git pull ${UPSTREAM_REPO} ${TARGET_BRANCH}-external-fixes
fi

# Try to merge fixes from local repo if this isn't a merge request
# otherwise try merging the fixes from the merge target
if [ -z "$CI_MERGE_REQUEST_PROJECT_PATH" ]; then
    if [ "$(git ls-remote --exit-code --heads origin ${TARGET_BRANCH}-external-fixes)" ]; then
        git pull origin ${TARGET_BRANCH}-external-fixes
    fi
else
    if [ "$(git ls-remote --exit-code --heads ${CI_MERGE_REQUEST_PROJECT_URL} ${CI_MERGE_REQUEST_TARGET_BRANCH_NAME}-external-fixes)" ]; then
        git pull ${CI_MERGE_REQUEST_PROJECT_URL} ${CI_MERGE_REQUEST_TARGET_BRANCH_NAME}-external-fixes
    fi
fi

if [[ -n "${MERGE_FRAGMENT}" ]]; then
    ./scripts/kconfig/merge_config.sh ${DEFCONFIG} drivers/gpu/drm/ci/${MERGE_FRAGMENT}
else
    make `basename ${DEFCONFIG}`
fi

for opt in $ENABLE_KCONFIGS; do
    ./scripts/config --enable CONFIG_$opt
done
for opt in $DISABLE_KCONFIGS; do
    ./scripts/config --disable CONFIG_$opt
done

make ${KERNEL_IMAGE_NAME}

mkdir -p /lava-files/
for image in ${KERNEL_IMAGE_NAME}; do
    cp arch/${KERNEL_ARCH}/boot/${image} /lava-files/.
done

if [[ -n ${DEVICE_TREES} ]]; then
    make dtbs
    cp ${DEVICE_TREES} /lava-files/.
fi

make modules
mkdir -p install/modules/
INSTALL_MOD_PATH=install/modules/ make modules_install

if [[ ${DEBIAN_ARCH} = "arm64" ]]; then
    make Image.lzma
    mkimage \
        -f auto \
        -A arm \
        -O linux \
        -d arch/arm64/boot/Image.lzma \
        -C lzma\
        -b arch/arm64/boot/dts/qcom/sdm845-cheza-r3.dtb \
        /lava-files/cheza-kernel
    KERNEL_IMAGE_NAME+=" cheza-kernel"

    # Make a gzipped copy of the Image for db410c.
    gzip -k /lava-files/Image
    KERNEL_IMAGE_NAME+=" Image.gz"
fi

# Pass needed files to the test stage
mkdir -p install
cp -rfv .gitlab-ci/* install/.
cp -rfv install/common install/ci-common
cp -rfv drivers/gpu/drm/ci/* install/.

. .gitlab-ci/container/container_post_build.sh

if [[ "$UPLOAD_TO_MINIO" = "1" ]]; then
    xz -7 -c -T${FDO_CI_CONCURRENT:-4} vmlinux > /lava-files/vmlinux.xz
    FILES_TO_UPLOAD="$KERNEL_IMAGE_NAME vmlinux.xz"

    if [[ -n $DEVICE_TREES ]]; then
        FILES_TO_UPLOAD="$FILES_TO_UPLOAD $(basename -a $DEVICE_TREES)"
    fi

    for f in $FILES_TO_UPLOAD; do
        ci-fairy s3cp --token-file "${CI_JOB_JWT_FILE}" /lava-files/$f \
                https://${PIPELINE_ARTIFACTS_BASE}/${DEBIAN_ARCH}/$f
    done

    S3_ARTIFACT_NAME="kernel-files.tar.zst"
    tar --zstd -cf $S3_ARTIFACT_NAME install
    ci-fairy s3cp --token-file "${CI_JOB_JWT_FILE}" ${S3_ARTIFACT_NAME} https://${PIPELINE_ARTIFACTS_BASE}/${DEBIAN_ARCH}/${S3_ARTIFACT_NAME}

    echo "Download vmlinux.xz from https://${PIPELINE_ARTIFACTS_BASE}/${DEBIAN_ARCH}/vmlinux.xz"
fi

mkdir -p artifacts/install/lib
mv install/* artifacts/install/.
rm -rf artifacts/install/modules
ln -s common artifacts/install/ci-common
cp .config artifacts/${CI_JOB_NAME}_config

for image in ${KERNEL_IMAGE_NAME}; do
    cp /lava-files/$image artifacts/install/.
done

tar -C artifacts -cf artifacts/install.tar install
rm -rf artifacts/install
