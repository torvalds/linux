#!/bin/sh

make ARCH=arm -j3
cp ./arch/arm/boot/zImage /media/xerpi/GATEWAYNAND/
sync
echo "Copied!"
