#!/bin/sh

# /etc/pm/sleep.d/60_wpa_supplicant
# Action script to notify wpa_supplicant of pm-action events.

PATH=/sbin:/usr/sbin:/bin:/usr/bin

WPACLI=wpa_cli

case "$1" in
	suspend|hibernate)
		$WPACLI suspend
		;;
	resume|thaw)
		$WPACLI resume
		;;
esac

exit 0
