#!/bin/bash

linux_dir=/net/rwlab-srv1/nx_share/linux
ARCH=${ARCH:-x86}
CROSS_COMPILE=${CROSS_COMPILE:-x86_64-poky-linux-}
error=0

if [ ! -d $linux_dir ]
then
    echo "Invalid path: ${linux_dir}" >&2
    exit 1
fi

for version in $(find $linux_dir -maxdepth 2 -type d -name cevav7 | sort)
do
    echo ""
    echo "#####################################################"
    echo "Testing $version"
    echo "#####################################################"
    ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE KERNELDIR=$version \
    CONFIG_ECRNX_SOFTMAC=m CONFIG_ECRNX_FULLMAC=m CONFIG_ECRNX_FHOST=m make -j 8

    if [ $? -ne  0 ]
    then
	((error++))
    fi
done

exit $error
