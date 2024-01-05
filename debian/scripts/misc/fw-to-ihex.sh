#!/bin/bash

F=$1
if [ "$F" = "" ]
then
	echo You must supply a firmware file.
	exit 1
fi

echo "unsigned char d[] = {" > $F.c
hexdump -v -e '"\t" 8/1 "0x%02x, " "\n"' $F >> $F.c
echo "};" >> $F.c
sed -i 's/0x .*$//' $F.c

O="`dirname $F`/`basename $F`.o"
gcc -o $O -c $F.c
objcopy -Oihex $F.o $F.ihex

