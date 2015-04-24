#!/bin/sh

make ARCH=arm -j3
make ARCH=arm nintendo3ds_ctr.dtb
cp ./arch/arm/boot/zImage "/media/$USER/GATEWAYNAND/"
cp ./arch/arm/boot/dts/nintendo3ds_ctr.dtb "/media/$USER/GATEWAYNAND/"
sync
echo "Copied!"
