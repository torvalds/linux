#!/bin/sh

# I use this to build static uclibc based binary using Aboriginal Linux toolchain:
PREFIX="i686-"
STATIC="-static"
# Standard build:
#PREFIX=""
#STATIC=""

${PREFIX}gcc -Os -Wall -I wolfssl-* -c ssl_helper.c -o ssl_helper.o
${PREFIX}gcc $STATIC --start-group ssl_helper.o -lm wolfssl-*/src/.libs/libwolfssl.a --end-group -o ssl_helper
${PREFIX}strip ssl_helper
