#!/bin/sh

ttyname=`tty`
ttybase="${ttyname%%[0123456789]*}"     # strip numeric tail

if test "$ttybase" = "/dev/tty"; then
	tail="${ttyname:8}"
	echo "* Setting terminal device's owner to $LOGIN_UID:$LOGIN_GID"
	test -c "/dev/vcs$tail" && chown "$LOGIN_UID:$LOGIN_GID" "/dev/vcs$tail"
	test -c "/dev/vcsa$tail" && chown "$LOGIN_UID:$LOGIN_GID" "/dev/vcsa$tail"
fi
# We can do this also, but login does it itself
# chown "$LOGIN_UID:$LOGIN_GID" "$ttyname"
