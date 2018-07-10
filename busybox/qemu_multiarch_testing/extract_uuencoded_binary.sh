#!/bin/sh
# Extract uuencoded and bzipped busybox binaries
# from system-image-*.log files

for logfile in system-image-*.log; do
	grep -q '^begin 744 busybox.bz2' "$logfile" \
	|| { echo "No busybox.bz2 in $logfile"; continue; }

	arch=${logfile%.log}
	arch=${arch#system-image-}

	test -e "busybox-$arch" \
	&& { echo "busybox-$arch exists, not overwriting"; continue; }

	uudecode -o - "$logfile" | bunzip2 >"busybox-$arch" \
	&& chmod 755 "busybox-$arch"
done
