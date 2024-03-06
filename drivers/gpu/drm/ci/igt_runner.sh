#!/bin/sh
# SPDX-License-Identifier: MIT

set -ex

export IGT_FORCE_DRIVER=${DRIVER_NAME}
export PATH=$PATH:/igt/bin/
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/igt/lib/aarch64-linux-gnu/:/igt/lib/x86_64-linux-gnu:/igt/lib:/igt/lib64

# Uncomment the below to debug problems with driver probing
: '
ls -l /dev/dri/
cat /sys/kernel/debug/devices_deferred
cat /sys/kernel/debug/device_component/*
'

# Dump drm state to confirm that kernel was able to find a connected display:
set +e
cat /sys/kernel/debug/dri/*/state
set -e

case "$DRIVER_NAME" in
    rockchip|meson)
        export IGT_FORCE_DRIVER="panfrost"
        ;;
    mediatek)
        if [ "$GPU_VERSION" = "mt8173" ]; then
            export IGT_FORCE_DRIVER=${DRIVER_NAME}
        elif [ "$GPU_VERSION" = "mt8183" ]; then
            export IGT_FORCE_DRIVER="panfrost"
        fi
        ;;
    amdgpu)
        # Cannot use HWCI_KERNEL_MODULES as at that point we don't have the module in /lib
        mv /install/modules/lib/modules/* /lib/modules/.
        modprobe amdgpu
        ;;
esac

if [ -e "/install/xfails/$DRIVER_NAME-$GPU_VERSION-skips.txt" ]; then
    IGT_SKIPS="--skips /install/xfails/$DRIVER_NAME-$GPU_VERSION-skips.txt"
fi

if [ -e "/install/xfails/$DRIVER_NAME-$GPU_VERSION-flakes.txt" ]; then
    IGT_FLAKES="--flakes /install/xfails/$DRIVER_NAME-$GPU_VERSION-flakes.txt"
fi

if [ -e "/install/xfails/$DRIVER_NAME-$GPU_VERSION-fails.txt" ]; then
    IGT_FAILS="--baseline /install/xfails/$DRIVER_NAME-$GPU_VERSION-fails.txt"
fi

if [ "`uname -m`" = "aarch64" ]; then
    ARCH="arm64"
elif [ "`uname -m`" = "armv7l" ]; then
    ARCH="arm"
else
    ARCH="x86_64"
fi

curl -L --retry 4 -f --retry-all-errors --retry-delay 60 -s ${FDO_HTTP_CACHE_URI:-}$PIPELINE_ARTIFACTS_BASE/$ARCH/igt.tar.gz | tar --zstd -v -x -C /


# If the job is parallel at the gitab job level, take the corresponding fraction
# of the caselist.
if [ -n "$CI_NODE_INDEX" ]; then
    sed -ni $CI_NODE_INDEX~$CI_NODE_TOTAL"p" /install/testlist.txt
fi

# core_getversion checks if the driver is loaded and probed correctly
# so run it in all shards
if ! grep -q "core_getversion" /install/testlist.txt; then
    # Add the line to the file
    echo "core_getversion" >> /install/testlist.txt
fi

set +e
igt-runner \
    run \
    --igt-folder /igt/libexec/igt-gpu-tools \
    --caselist /install/testlist.txt \
    --output /results \
    $IGT_SKIPS \
    $IGT_FLAKES \
    $IGT_FAILS \
    --jobs 1
ret=$?
set -e

deqp-runner junit \
   --testsuite IGT \
   --results /results/failures.csv \
   --output /results/junit.xml \
   --limit 50 \
   --template "See https://$CI_PROJECT_ROOT_NAMESPACE.pages.freedesktop.org/-/$CI_PROJECT_NAME/-/jobs/$CI_JOB_ID/artifacts/results/{{testcase}}.xml"

# Store the results also in the simpler format used by the runner in ChromeOS CI
#sed -r 's/(dmesg-warn|pass)/success/g' /results/results.txt > /results/results_simple.txt

cd $oldpath
exit $ret
