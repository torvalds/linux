#!/bin/sh

PREFIX="i486-linux-uclibc-"
OPTS="-static -static-libgcc \
-D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 \
-Wall -Wshadow -Wwrite-strings -Wundef -Wstrict-prototypes -Werror \
-Wold-style-definition -Wdeclaration-after-statement -Wno-pointer-sign \
-Wmissing-prototypes -Wmissing-declarations \
-Os -fno-builtin-strlen -finline-limit=0 -fomit-frame-pointer \
-ffunction-sections -fdata-sections -fno-guess-branch-probability \
-funsigned-char \
-falign-functions=1 -falign-jumps=1 -falign-labels=1 -falign-loops=1 \
-march=i386 -mpreferred-stack-boundary=2 \
-Wl,--warn-common -Wl,--sort-common -Wl,--gc-sections"

${PREFIX}gcc \
${OPTS} \
-Wl,-Map -Wl,index.cgi.map \
httpd_indexcgi.c -o index.cgi && strip index.cgi

${PREFIX}gcc \
${OPTS} \
-Wl,-Map -Wl,httpd_ssi.map \
httpd_ssi.c -o httpd_ssi && strip httpd_ssi
