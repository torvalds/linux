#!/bin/sh

echo "Begin to make MV8686 wifi driver package ..."

mkdir -p mv8686
rm -f mv8686/*

cp ReleaseNotes.txt mv8686/
cp Makefile.mv8686 mv8686/Makefile
cp ../wifi_power/wifi_power.h mv8686/
cp ../wifi_power/wifi_power.c mv8686/
cp ../wifi_power/wifi_config.c mv8686/
uuencode mv8686.o mv8686.o > mv8686.uu
cp mv8686.uu mv8686/

echo "Done"

