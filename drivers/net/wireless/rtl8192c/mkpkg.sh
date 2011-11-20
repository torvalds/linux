#!/bin/sh

PKGNAME="rtl8192c"
RKPUB="rkpub"
PKGVER=`grep -r '#define RTL8192_DRV_VERSION' os_dep/linux/wifi_version.h | sed 's/[^"]*\"\([_0-9A-Za-z\.]*\)*\"/\1/'`
if [ $1 ]
then
RKPLAT=$1
else
RKPLAT="rk29"
fi

BUILDTIME=`date +%Y``date +%m``date +%d`

[ `dirname $0` = '.' ] || exit

echo "Begin to make ${PKGNAME} WiFi driver package ..."

rm -rf ${RKPUB}
mkdir -p ${RKPUB}/${PKGNAME}

uuencode os_dep/linux/ioctl_linux.o os_dep/linux/ioctl_linux.o > ioctl_linux.uu
uuencode core/rtw_ioctl_set.o core/rtw_ioctl_set.o > rtw_ioctl_set.uu

cp Makefile.${PKGNAME}_v2 ${RKPUB}/${PKGNAME}/Makefile
cp Kconfig ${RKPUB}/${PKGNAME}/Kconfig
cp -a core hal include os_dep ${RKPUB}/${PKGNAME}/
rm -f ${RKPUB}/${PKGNAME}/os_dep/linux/ioctl_linux.*
rm -f ${RKPUB}/${PKGNAME}/core/rtw_ioctl_set.*
cp ioctl_linux.uu ${RKPUB}/${PKGNAME}/os_dep/linux/
cp rtw_ioctl_set.uu ${RKPUB}/${PKGNAME}/core/

find rkpub -name '*.o' -exec rm -f {} \;
find rkpub -name '.*.cmd' -exec rm -f {} \;

#cp os_dep/linux/wifi_version.h ${RKPUB}/${PKGNAME}/
#cp os_dep/linux/wifi_power.c ${RKPUB}/${PKGNAME}/
#cp os_dep/linux/wifi_power.h ${RKPUB}/${PKGNAME}/
#cp os_dep/linux/wifi_power_usb.c ${RKPUB}/${PKGNAME}/
#cp os_dep/linux/wifi_power_ops.c ${RKPUB}/${PKGNAME}/
#cp Kconfig ${RKPUB}/${PKGNAME}/Kconfig

#cp -dpR os_dep/linux ${RKPUB}/${PKGNAME}/
#find ${RKPUB}/${PKGNAME}/rkcfg -type f -name '*.o*' | xargs rm -rf
#find ${RKPUB}/${PKGNAME}/rkcfg -type d -name '.svn' | xargs rm -rf
#rm ${RKPUB}/${PKGNAME}/rkcfg/src/wifi_power_ops.c

#uuencode ${PKGNAME}u.o ${PKGNAME}u.o > ${RKPUB}/${PKGNAME}/${PKGNAME}.uu

cd ${RKPUB} && tar -zcvf ${PKGNAME}.${RKPLAT}-${PKGVER}-${BUILDTIME}.tgz ${PKGNAME}

echo "Done"

