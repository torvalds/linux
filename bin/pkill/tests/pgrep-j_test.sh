#!/bin/sh
# $FreeBSD$

jail_name_to_jid()
{
	local check_name="$1"
	jls -j "$check_name" -s | tr ' ' '\n' | grep jid= | sed -e 's/.*=//g'
}

base=pgrep_j_test

if [ `id -u` -ne 0 ]; then
	echo "1..0 # skip Test needs uid 0."
	exit 0
fi

echo "1..4"

sleep=$(pwd)/sleep.txt
ln -sf /bin/sleep $sleep

name="pgrep -j <jid>"
sleep_amount=15
jail -c path=/ name=${base}_1_1 ip4.addr=127.0.0.1 \
    command=daemon -p ${PWD}/${base}_1_1.pid $sleep $sleep_amount &

jail -c path=/ name=${base}_1_2 ip4.addr=127.0.0.1 \
    command=daemon -p ${PWD}/${base}_1_2.pid $sleep $sleep_amount &
sleep 0.5

for i in `seq 1 10`; do
	jid1=$(jail_name_to_jid ${base}_1_1)
	jid2=$(jail_name_to_jid ${base}_1_2)
	jid="${jid1},${jid2}"
	case "$jid" in
	[0-9]+,[0-9]+)
		break
		;;
	esac
	sleep 0.1
done
sleep 0.5

pid1="$(pgrep -f -x -j "$jid" "$sleep $sleep_amount" | sort)"
pid2=$(printf "%s\n%s" "$(cat ${PWD}/${base}_1_1.pid)" \
    $(cat ${PWD}/${base}_1_2.pid) | sort)
if [ "$pid1" = "$pid2" ]; then
	echo "ok 1 - $name"
else
	echo "not ok 1 - $name # pgrep output: '$(echo $pid1)', pidfile output: '$(echo $pid2)'"
fi
[ -f ${PWD}/${base}_1_1.pid ] && kill $(cat ${PWD}/${base}_1_1.pid)
[ -f ${PWD}/${base}_1_2.pid ] && kill $(cat ${PWD}/${base}_1_2.pid)
wait

name="pgrep -j any"
sleep_amount=16
jail -c path=/ name=${base}_2_1 ip4.addr=127.0.0.1 \
    command=daemon -p ${PWD}/${base}_2_1.pid $sleep $sleep_amount &

jail -c path=/ name=${base}_2_2 ip4.addr=127.0.0.1 \
    command=daemon -p ${PWD}/${base}_2_2.pid $sleep $sleep_amount &

sleep 2
pid1="$(pgrep -f -x -j any "$sleep $sleep_amount" | sort)"
pid2=$(printf "%s\n%s" "$(cat ${PWD}/${base}_2_1.pid)" \
    $(cat ${PWD}/${base}_2_2.pid) | sort)
if [ "$pid1" = "$pid2" ]; then
	echo "ok 2 - $name"
else
	echo "not ok 2 - $name # pgrep output: '$(echo $pid1)', pidfile output: '$(echo $pid2)'"
fi
[ -f ${PWD}/${base}_2_1.pid ] && kill $(cat ${PWD}/${base}_2_1.pid)
[ -f ${PWD}/${base}_2_2.pid ] && kill $(cat ${PWD}/${base}_2_2.pid)
wait

name="pgrep -j none"
sleep_amount=17
daemon -p ${PWD}/${base}_3_1.pid $sleep $sleep_amount &
jail -c path=/ name=${base}_3_2 ip4.addr=127.0.0.1 \
    command=daemon -p ${PWD}/${base}_3_2.pid $sleep $sleep_amount &
sleep 2
pid="$(pgrep -f -x -j none "$sleep $sleep_amount")"
if [ "$pid" = "$(cat ${PWD}/${base}_3_1.pid)" ]; then
	echo "ok 3 - $name"
else
	echo "not ok 3 - $name # pgrep output: '$(echo $pid1)', pidfile output: '$(echo $pid2)'"
fi
[ -f ${PWD}/${base}_3_1.pid ] && kill $(cat $PWD/${base}_3_1.pid) 
[ -f ${PWD}/${base}_3_2.pid ] && kill $(cat $PWD/${base}_3_2.pid) 
wait

# test 4 is like test 1 except with jname instead of jid.
name="pgrep -j <jname>"
sleep_amount=18
jail -c path=/ name=${base}_4_1 ip4.addr=127.0.0.1 \
    command=daemon -p ${PWD}/${base}_4_1.pid $sleep $sleep_amount &

jail -c path=/ name=${base}_4_2 ip4.addr=127.0.0.1 \
    command=daemon -p ${PWD}/${base}_4_2.pid $sleep $sleep_amount &

sleep 0.5

jname="${base}_4_1,${base}_4_2"
pid1="$(pgrep -f -x -j "$jname" "$sleep $sleep_amount" | sort)"
pid2=$(printf "%s\n%s" "$(cat ${PWD}/${base}_4_1.pid)" \
    $(cat ${PWD}/${base}_4_2.pid) | sort)
if [ "$pid1" = "$pid2" ]; then
	echo "ok 4 - $name"
else
	echo "not ok 4 - $name # pgrep output: '$(echo $pid1)', pidfile output: '$(echo $pid2)'"
fi
[ -f ${PWD}/${base}_4_1.pid ] && kill $(cat ${PWD}/${base}_4_1.pid)
[ -f ${PWD}/${base}_4_2.pid ] && kill $(cat ${PWD}/${base}_4_2.pid)
wait

rm -f $sleep
