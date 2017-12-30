#!/bin/bash
(
cd $(dirname $0)
kerneldir=$(dirname $(pwd))
cd cryptodev-linux-1.9
make KERNEL_DIR=$kerneldir CROSS_COMPILE=arm-linux-gnueabihf- ARCH=arm
if [[ $? == 0 ]];then
  echo "build successful"
  #cp cryptodev.ko $kerneldir/../SD/BPI-ROOT/lib/modules/
  cp cryptodev.ko ../
fi
echo "build test-tool"
cd tests
make KERNEL_DIR=$kerneldir CROSS_COMPILE=arm-linux-gnueabihf- ARCH=arm
if [[ $? == 0 ]];then
  echo "build successful"
  tar -czf ../../cryptodev_test.tar.gz {async_cipher,async_hmac,async_speed,cipher,cipher-aead,cipher-aead-srtp,cipher_comp,cipher-gcm,fullspeed,hash_comp,hashcrypt_speed,hmac,hmac_comp,sha_speed,speed}
fi
)
