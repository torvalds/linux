#!/bin/sh

echo "Begin to make BCM4319 wifi driver package ..."

RKWLCFGDIR=../wifi_power
PKGNAME="bcm4319"
RKPUB="rkpub"
PKGVER=`grep -r '#define BCM4319_DRV_VERSION' ${RKWLCFGDIR}/wifi_version.h | sed 's/[^"]*\"\([0-9A-Za-z\.]*\)*\"/\1/'`

[ `dirname $0` = '.' ] || exit


rm -rf ${RKPUB}
mkdir -p ${RKPUB}/${PKGNAME}

cp Makefile.${PKGNAME} ${RKPUB}/${PKGNAME}/Makefile
cp Kconfig.${PKGNAME} ${RKPUB}/${PKGNAME}/Kconfig
cp ${RKWLCFGDIR}/wifi_power.c ${RKPUB}/${PKGNAME}/wifi_power.c
cp ${RKWLCFGDIR}/wifi_power.h ${RKPUB}/${PKGNAME}/wifi_power.h
cp ${RKWLCFGDIR}/wifi_version.h ${RKPUB}/${PKGNAME}/wifi_version.h
chmod 644 ${RKPUB}/${PKGNAME}/*
cp -dpR firmware ${RKPUB}/${PKGNAME}/
find ${RKPUB}/${PKGNAME}/firmware -type d -name '.svn' | xargs rm -rf
uuencode ${PKGNAME}.o ${PKGNAME}.o > ${RKPUB}/${PKGNAME}/${PKGNAME}.uu
cd ${RKPUB} && tar -jcvf ${PKGNAME}-${PKGVER}.tar.bz2 ${PKGNAME}

echo "Done"
