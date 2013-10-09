#!/bin/sh

gcc -o sembench sembench.c -O2 -lpthread

COMMAND="./sembench -t 200 -w 20 -r 30 -o 2"

echo -e "\n\t\tPass 1 without tracing"
$COMMAND
echo -e "\n\t\tPass 2 without tracing"
$COMMAND
echo -e "\n\t\tPass 3 without tracing"
$COMMAND

echo ""

KTAP_ONE_LINER="trace syscalls:sys_*_futex {}"

echo -e "\n\t\tPass 1 with tracing"
../../ktap -e "$KTAP_ONE_LINER" -- $COMMAND
echo -e "\n\t\tPass 2 with tracing"
../../ktap -e "$KTAP_ONE_LINER" -- $COMMAND
echo -e "\n\t\tPass 3 with tracing"
../../ktap -e "$KTAP_ONE_LINER" -- $COMMAND

rm -rf ./sembench
