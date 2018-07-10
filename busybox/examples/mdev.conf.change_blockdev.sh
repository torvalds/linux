#!/bin/sh

# Seconds to try to reread partition table
cnt=60

exec </dev/null
exec >"/tmp/${0##*/}.$$.out"
exec 2>&1

(
echo "Running: $0"
echo "Env:"
env | sort

while sleep 1; test $cnt != 0; do
	echo "Trying to reread partition table on $DEVNAME ($cnt)"
	cnt=$((cnt-1))
	# If device node doesn't exist, it means the device was removed.
	# Stop trying.
	test -e "$DEVNAME" || { echo "$DEVNAME doesn't exist, aborting"; exit 1; }
	#echo "$DEVNAME exists"
	if blockdev --rereadpt "$DEVNAME"; then
		echo "blockdev --rereadpt succeeded"
		exit 0
	fi
	echo "blockdev --rereadpt failed, exit code: $?"
done
echo "Timed out"
) &
