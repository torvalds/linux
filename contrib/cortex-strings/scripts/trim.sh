#!/bin/bash
#
# Trims the whitespace from around any given images
#

for i in $@; do
    convert $i -bordercolor white -border 1x1 -trim +repage -alpha off +dither -colors 32 PNG8:next-$i
    mv next-$i $i
done
