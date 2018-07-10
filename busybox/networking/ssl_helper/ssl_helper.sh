#!/bin/sh

# I use this to build static uclibc based binary using Aboriginal Linux toolchain:
PREFIX=x86_64-
STATIC=-static
# Standard build:
PREFIX=""
STATIC=""

${PREFIX}gcc -Os -DPOSIX -I.. -I../sampleCerts -Wall -c ssl_helper.c -o ssl_helper.o
${PREFIX}gcc $STATIC ssl_helper.o ../libmatrixssl.a -lc ../libmatrixssl.a -o ssl_helper
