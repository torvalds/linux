#!/bin/sh

make ARCH=arm -j3
make ARCH=arm nintendo3ds_ctr.dtb
cp ./arch/arm/boot/zImage "/run/media/$USER/GATEWAYNAND/"
cp ./arch/arm/boot/dts/nintendo3ds_ctr.dtb "/run/media/$USER/GATEWAYNAND/"
sync
echo "Copied!"
