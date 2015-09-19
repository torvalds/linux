#!/bin/sh

CROSS_PATH="/opt/x-tools/arm-unknown-linux-gnueabi/bin/"

export PATH=$PATH:$CROSS_PATH
make ARCH=arm CROSS_COMPILE=arm-unknown-linux-gnueabi- nintendo3ds_ctr.dtb
cp ./arch/arm/boot/dts/nintendo3ds_ctr.dtb "/mnt/GATEWAYNAND/"
sync
echo "Copied!"
