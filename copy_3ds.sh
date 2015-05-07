#!/bin/sh

make ARCH=arm -j3
make ARCH=arm nintendo3ds_ctr.dtb
cp ./arch/arm/boot/zImage "/mnt/GATEWAYNAND/"
cp ./arch/arm/boot/dts/nintendo3ds_ctr.dtb "/mnt/GATEWAYNAND/"
sync
echo "Copied!"
