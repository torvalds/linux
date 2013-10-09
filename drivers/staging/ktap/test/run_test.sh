#!/bin/sh

rmmod ktapvm > /dev/null 2>&1
insmod ../ktapvm.ko
if test $? -ne 0; then
	echo "Cannot insmod ../ktapvm.ko"
	exit -1
fi

KTAP=../ktap
function ktaprun {
	echo "$KTAP $@"
	$KTAP $@
}



#######################################################
# Use $ktap directly if the arguments contains strings
$KTAP arg.kp 1 testing "2 3 4"
$KTAP -e 'print("one-liner testing")'
$KTAP -e 'exit()'
$KTAP -o /dev/null -e 'trace syscalls:* { print(argevent) }' \
		-- ls > /devnull

$KTAP -o /dev/null -e 'trace syscalls:* { print(argevent) }' \
		-- $KTAP -e 'while (1) {}'

ktaprun arith.kp
ktaprun concat.kp
ktaprun count.kp
ktaprun fibonacci.kp
ktaprun function.kp
ktaprun if.kp
ktaprun kprobe.kp
ktaprun kretprobe.kp
ktaprun len.kp
ktaprun looping.kp
ktaprun pairs.kp
ktaprun table.kp
ktaprun aggr_table.kp
ktaprun timer.kp
ktaprun tracepoint.kp
ktaprun -o /dev/null zerodivide.kp
ktaprun ansi.kp

echo "testing kill deadloop ktap script"
$KTAP -e 'while (1) {}' &
pkill ktap
sleep 1

#####################################################
rmmod ktapvm
if test $? -ne 0; then
	echo "Error in rmmod ../ktapvm.ko, leak module refcount?"
	exit -1
fi

