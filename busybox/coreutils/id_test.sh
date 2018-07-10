#!/bin/bash
# Test script for busybox id vs. coreutils id.
# Needs root privileges for some tests.

cp /usr/bin/id .
BUSYBOX=./busybox
ID=./id
LIST=`awk -F: '{ printf "%s\n", $1 }' /etc/passwd`
FLAG_USER_EXISTS="no"
TEST_USER="f583ca884c1d93458fb61ed137ff44f6"

echo "test 1: id [options] nousername"
rm -f foo bar
for OPTIONS in "" "-u" "-un" "-unr" "-g" "-gn" "-gnr" "-G" "-Gn" "-Gnr"
do
	#echo "$OPTIONS"
	$BUSYBOX id $OPTIONS >foo 2>/dev/null
	RET1=$?
	$ID $OPTIONS >bar 2>/dev/null
	RET2=$?
	if test "$RET1" != "$RET2"; then
		echo "Return Values differ ($RET1 != $RET2): options $OPTIONS"
	fi
	diff foo bar
done

echo "test 2: id [options] username"
rm -f foo bar
for OPTIONS in "" "-u" "-un" "-unr" "-g" "-gn" "-gnr" "-G" "-Gn" "-Gnr"
do
	#echo "$OPTIONS"
	for i in $LIST ; do
		if test "$i" = "$TEST_USER"; then
			FLAG_USER_EXISTS="yes"
		fi
		$BUSYBOX id $OPTIONS $i >foo 2>/dev/null
		RET1=$?
		$ID $OPTIONS $i >bar 2>/dev/null
		RET2=$?
		if test "$RET1" != "$RET2"; then
			echo "Return Values differ ($RET1 != $RET2): options $OPTIONS"
		fi
		diff foo bar
	done
done

if test $FLAG_USER_EXISTS = "yes"; then
	echo "test 3,4,5,6,7,8,9,10,11,12 skipped because test user $TEST_USER already exists"
	rm -f foo bar
	exit 1
fi

adduser -s /bin/true -g "" -H -D "$TEST_USER" || exit 1

chown $TEST_USER.$TEST_USER $BUSYBOX
chmod u+s $BUSYBOX 2>&1 /dev/null
chown $TEST_USER.$TEST_USER $ID
chmod u+s $ID 2>&1 /dev/null

echo "test 3 setuid, existing user: id [options] no username"
rm -f foo bar
for OPTIONS in "" "-u" "-un" "-unr" "-g" "-gn" "-gnr" "-G" "-Gn" "-Gnr"
do
	#echo "$OPTIONS"
	$BUSYBOX id $OPTIONS >foo 2>/dev/null
	RET1=$?
	$ID $OPTIONS >bar 2>/dev/null
	RET2=$?
	if test "$RET1" != "$RET2"; then
		echo "Return Values differ ($RET1 != $RET2): options $OPTIONS"
	fi
	diff foo bar
	#done
done

echo "test 4 setuid, existing user: id [options] username"
rm -f foo bar
for OPTIONS in "" "-u" "-un" "-unr" "-g" "-gn" "-gnr" "-G" "-Gn" "-Gnr"
do
	#echo "$OPTIONS"
	for i in $LIST ; do
		$BUSYBOX id $OPTIONS $i >foo 2>/dev/null
		RET1=$?
		$ID $OPTIONS $i >bar 2>/dev/null
		RET2=$?
		if test "$RET1" != "$RET2"; then
			echo "Return Values differ ($RET1 != $RET2): options $OPTIONS"
		fi
		diff foo bar
	done
done

chown $TEST_USER.$TEST_USER $BUSYBOX
chmod g+s $BUSYBOX 2>&1 /dev/null
chown $TEST_USER.$TEST_USER $ID
chmod g+s $ID 2>&1 /dev/null

echo "test 5 setgid, existing user: id [options] no username"
rm -f foo bar
for OPTIONS in "" "-u" "-un" "-unr" "-g" "-gn" "-gnr" "-G" "-Gn" "-Gnr"
do
	#echo "$OPTIONS"
	$BUSYBOX id $OPTIONS >foo 2>/dev/null
	RET1=$?
	$ID $OPTIONS >bar 2>/dev/null
	RET2=$?
	if test "$RET1" != "$RET2"; then
		echo "Return Values differ ($RET1 != $RET2): options $OPTIONS"
	fi
	diff foo bar
	#done
done

echo "test 6 setgid, existing user: id [options] username"
rm -f foo bar
for OPTIONS in "" "-u" "-un" "-unr" "-g" "-gn" "-gnr" "-G" "-Gn" "-Gnr"
do
	#echo "$OPTIONS"
	for i in $LIST ; do
		$BUSYBOX id $OPTIONS $i >foo 2>/dev/null
		RET1=$?
		$ID $OPTIONS $i >bar 2>/dev/null
		RET2=$?
		if test "$RET1" != "$RET2"; then
			echo "Return Values differ ($RET1 != $RET2): options $OPTIONS"
		fi
		diff foo bar
	done
done

chown $TEST_USER.$TEST_USER $BUSYBOX
chmod u+s,g+s $BUSYBOX 2>&1 /dev/null
chown $TEST_USER.$TEST_USER $ID
chmod u+s,g+s $ID 2>&1 /dev/null

echo "test 7 setuid, setgid, existing user: id [options] no username"
rm -f foo bar
for OPTIONS in "" "-u" "-un" "-unr" "-g" "-gn" "-gnr" "-G" "-Gn" "-Gnr"
do
	#echo "$OPTIONS"
	$BUSYBOX id $OPTIONS >foo 2>/dev/null
	RET1=$?
	$ID $OPTIONS >bar 2>/dev/null
	RET2=$?
	if test "$RET1" != "$RET2"; then
		echo "Return Values differ ($RET1 != $RET2): options $OPTIONS"
	fi
	diff foo bar
	#done
done

echo "test 8 setuid, setgid, existing user: id [options] username"
rm -f foo bar
for OPTIONS in "" "-u" "-un" "-unr" "-g" "-gn" "-gnr" "-G" "-Gn" "-Gnr"
do
	#echo "$OPTIONS"
	for i in $LIST ; do
		$BUSYBOX id $OPTIONS $i >foo 2>/dev/null
		RET1=$?
		$ID $OPTIONS $i >bar 2>/dev/null
		RET2=$?
		if test "$RET1" != "$RET2"; then
			echo "Return Values differ ($RET1 != $RET2): options $OPTIONS"
		fi
		diff foo bar
	done
done

deluser $TEST_USER || exit 1

echo "test 9 setuid, setgid, not existing user: id [options] no username"
rm -f foo bar
for OPTIONS in "" "-u" "-un" "-unr" "-g" "-gn" "-gnr" "-G" "-Gn" "-Gnr"
do
	#echo "$OPTIONS"
	$BUSYBOX id $OPTIONS >foo 2>/dev/null
	RET1=$?
	$ID $OPTIONS >bar 2>/dev/null
	RET2=$?
	if test "$RET1" != "$RET2"; then
		echo "Return Values differ ($RET1 != $RET2): options $OPTIONS"
	fi
	diff foo bar
done

echo "test 10 setuid, setgid, not existing user: id [options] username"
rm -f foo bar
for OPTIONS in "" "-u" "-un" "-unr" "-g" "-gn" "-gnr" "-G" "-Gn" "-Gnr"
do
	#echo "$OPTIONS"
	for i in $LIST ; do
		$BUSYBOX id $OPTIONS $i >foo 2>/dev/null
		RET1=$?
		$ID $OPTIONS $i >bar 2>/dev/null
		RET2=$?
		if test "$RET1" != "$RET2"; then
			echo "Return Values differ ($RET1 != $RET2): options $OPTIONS"
		fi
		diff foo bar
	done
done

chown .root $BUSYBOX 2>&1 /dev/null
chown .root $ID 2>&1 /dev/null
chmod g+s $BUSYBOX 2>&1 /dev/null
chmod g+s $ID 2>&1 /dev/null

echo "test 11 setgid, not existing group: id [options] no username"
rm -f foo bar
for OPTIONS in "" "-u" "-un" "-unr" "-g" "-gn" "-gnr" "-G" "-Gn" "-Gnr"
do
	#echo "$OPTIONS"
	$BUSYBOX id $OPTIONS >foo 2>/dev/null
	RET1=$?
	$ID $OPTIONS >bar 2>/dev/null
	RET2=$?
	if test "$RET1" != "$RET2"; then
		echo "Return Values differ ($RET1 != $RET2): options $OPTIONS"
	fi
	diff foo bar
	#done
done

echo "test 12 setgid, not existing group: id [options] username"
rm -f foo bar
for OPTIONS in "" "-u" "-un" "-unr" "-g" "-gn" "-gnr" "-G" "-Gn" "-Gnr"
do
	#echo "$OPTIONS"
	for i in $LIST ; do
		$BUSYBOX id $OPTIONS $i >foo 2>/dev/null
		RET1=$?
		$ID $OPTIONS $i >bar 2>/dev/null
		RET2=$?
		if test "$RET1" != "$RET2"; then
			echo "Return Values differ ($RET1 != $RET2): options $OPTIONS"
		fi
		diff foo bar
	done
done

chown root.root $BUSYBOX 2>&1 /dev/null
chown root.root $ID 2>&1 /dev/null
rm -f $ID
rm -f foo bar
