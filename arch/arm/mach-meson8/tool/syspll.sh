#!/bin/bash

#0x41200200 = 1092616704 (Fout >= 1.25G, OD = 0)
FOUT_OVER_1G25=1092616704
#0x41210200 = 1092682240 (Fout < 1.25G, OD = 1)
FOUT_BELOW_1G25=1092682240

cntl0=0
m=1
mm=1
f=0
latency=222
l2=3
axi=5
peri=4
apb=6
while [ "$m" -le 88 ]
do
    let "f = $m * 24"
    if [ "$f" -le 1250 ]; then
        let "cntl0_init = $FOUT_BELOW_1G25"        
        let "mm = $m * 2"
    else
        let "cntl0_init = $FOUT_OVER_1G25"
        let "mm = $m"
    fi
    let "cntl0 = $cntl0_init + $mm"
    if [ "$f" -ge 1680 ]; then
        latency=333;
    fi
    printf "\t\t{%4d, 0x%8X, 0x%d%d%d%d%d },\n" $f $cntl0 $latency $l2 $axi $peri $apb

    m=$((m+1))
done

#    printf "freq: %4d, cntl0: 0X%8x\n" $f $cntl0
