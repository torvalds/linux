#
# $Id$
#
# Copyright 2014, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

XO=$1
shift

XOP="${XO} --warn --depth 1 --leading-xpath /top"

${XO} --open top

NF=
for i in one:1:red two:2:blue three:3:green four:4:yellow ; do
    set `echo $i | sed 's/:/ /g'`
    ${XOP} ${NF} --wrap item \
        'Item {k:name} is {Lw:number}{:value/%03d/%d}, {Lwc:color}{:color}\n' \
         $1 $2 $3
    NF=--not-first
done

XOAN="${XO} --wrap anchor  --not-first --warn --depth 1"
${XOAN} "{[:18}{:address/%p}..{:foo/%u}{]:}\n" 0xdeadbeef 1
${XOAN} "{[:/18}{:address/%p}..{:foo/%u}{]:}\n" 0xdeadbeef 1
${XOAN} "{[:/%d}{:address/%p}..{:foo/%u}{]:}\n" 18 0xdeadbeef 1
${XOAN} "{[:/%s}{:address/%p}..{:foo/%u}{]:}\n" 18 0xdeadbeef 1

${XO} --close top
