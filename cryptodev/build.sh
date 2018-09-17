#!/bin/bash
(
cd $(dirname $0)
kerneldir=$(dirname $(pwd))
kernelver=$(cd ..;make kernelversion)
cd cryptodev-linux
make KERNEL_DIR=$kerneldir clean
make KERNEL_DIR=$kerneldir CROSS_COMPILE=arm-linux-gnueabihf- ARCH=arm
if [[ $? == 0 ]];then
  echo "build successful"
  #cp cryptodev.ko $kerneldir/../SD/BPI-ROOT/lib/modules/
  #cp cryptodev.ko ../
fi
# Remove Test Tool, is broken
#echo "build test-tool"
#cd tests
#make KERNEL_DIR=$kerneldir CROSS_COMPILE=arm-linux-gnueabihf- ARCH=arm
#if [[ $? == 0 ]];then
#  echo "build successful, packing..."
#  filename=../../cryptodev_test_$kernelver.tar.gz
#  tar -czf $filename {async_cipher,async_hmac,async_speed,cipher,cipher-aead,cipher-aead-srtp,cipher_comp,cipher-gcm,fullspeed,hash_comp,hashcrypt_speed,hmac,hmac_comp,sha_speed,speed}
#  md5sum $filename > $filename.md5
#fi
)
