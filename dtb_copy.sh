#!/bin/sh

make ARCH=arm CROSS_COMPILE=arm-unknown-linux-gnueabihf- nintendo3ds_ctr.dtb
cp ./arch/arm/boot/dts/nintendo3ds_ctr.dtb "/mnt/GATEWAYNAND/"
sync
echo "Copied!"
