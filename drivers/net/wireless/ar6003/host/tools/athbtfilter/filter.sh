
#!/bin/bash

mkdir ../athbtfilter.bk
cp -r ../athbtfilter ../athbtfilter.bk

mkdir include
mkdir host
mkdir host/include
mkdir host/os
mkdir host/os/linux
mkdir host/os/linux/include
mkdir host/tools
mkdir host/tools/athbtfilter

mv bluez host/tools/athbtfilter

cp Android.mk host
cp Android.mk host/tools
cp Android.mk host/tools/athbtfilter

cp ../../include/*.h host/include
cp ../../os/linux/include/*.h host/os/linux/include
cp ../../../include/*.h include
