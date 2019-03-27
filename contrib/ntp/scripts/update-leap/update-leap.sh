#!/bin/bash

# Copyright (C) 2014 Timothe Litt litt at acm dot org

# This script may be freely copied, used and modified providing that
# this notice and the copyright statement are included in all copies
# and derivative works.  No warranty is offered, and use is entirely at
# your own risk.  Bugfixes and improvements would be appreciated by the
# author.

VERSION="1.003"

# leap-seconds file manager/updater

# Depends on:
#  wget sed, tr, shasum, logger

# ########## Default configuration ##########
#
# Where to get the file
LEAPSRC="ftp://time.nist.gov/pub/leap-seconds.list"

# How many times to try to download new file
MAXTRIES=6
INTERVAL=10

# Where to find ntp config file
NTPCONF=/etc/ntp.conf

# How long before expiration to get updated file
PREFETCH="60 days"

# How to restart NTP - older NTP: service ntpd? try-restart | condrestart
# Recent NTP checks for new file daily, so there's nothing to do
RESTART=

# Where to put temporary copy before it's validated
TMPFILE="/tmp/leap-seconds.$$.tmp"

# Syslog facility
LOGFAC=daemon
# ###########################################

# Places to look for commands.  Allows for CRON having path to
# old utilities on embedded systems

PATHLIST="/opt/sbin:/opt/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:"

REQUIREDCMDS=" wget logger tr sed shasum"

SELF="`basename $0`"

function displayHelp {
            cat <<EOF
Usage: $SELF [options] [leapfile]

Verifies and if necessary, updates leap-second definition file

All arguments are optional:  Default (or current value) shown:
    -s    Specify the URL of the master copy to download
          $LEAPSRC
    -4    Use only IPv4
    -6    Use only IPv6
    -p 4|6
          Prefer IPv4 or IPv6 (as specified) addresses, but use either
    -d    Specify the filename on the local system
          $LEAPFILE
    -e    Specify how long before expiration the file is to be refreshed
          Units are required, e.g. "-e 60 days"  Note that larger values
          imply more frequent refreshes.
          "$PREFETCH"
    -f    Specify location of ntp.conf (used to make sure leapfile directive is
          present and to default  leapfile)
          $NTPCONF
    -F    Force update even if current file is OK and not close to expiring.
    -c    Command to restart NTP after installing a new file
          <none> - ntpd checks file daily
    -r    Specify number of times to retry on get failure
          $MAXTRIES
    -i    Specify number of minutes between retries
          $INTERVAL
    -l    Use syslog for output (Implied if CRONJOB is set)
    -L    Don't use syslog for output
    -P    Specify the syslog facility for logging
          $LOGFAC
    -t    Name of temporary file used in validation
          $TMPFILE
    -q    Only report errors to stdout
    -v    Verbose output
    -z    Specify path for utilities
          $PATHLIST
    -Z    Only use system path

$SELF will validate the file currently on the local system

Ordinarily, the file is found using the "leapfile" directive in $NTPCONF.
However, an alternate location can be specified on the command line.

If the file does not exist, is not valid, has expired, or is expiring soon,
a new copy will be downloaded.  If the new copy validates, it is installed and
NTP is (optionally) restarted.

If the current file is acceptable, no download or restart occurs.

-c can also be used to invoke another script to perform administrative
functions, e.g. to copy the file to other local systems.

This can be run as a cron job.  As the file is rarely updated, and leap
seconds are announced at least one month in advance (usually longer), it
need not be run more frequently than about once every three weeks.

For cron-friendly behavior, define CRONJOB=1 in the crontab.

This script depends on$REQUIREDCMDS

Version $VERSION
EOF
   return 0
}

# Default: Use syslog for logging if running under cron

SYSLOG="$CRONJOB"

if [ "$1" = "--help" ]; then
    displayHelp
    exit 0
fi

# Parse options

while getopts 46p:P:s:e:f:Fc:r:i:lLt:hqvz:Z opt; do
    case $opt in
        4)
            PROTO="-4"
            ;;
        6)
            PROTO="-6"
            ;;
        p)
            if [ "$OPTARG" = '4' -o "$OPTARG" = '6' ]; then
                PREFER="--prefer-family=IPv$OPTARG"
            else
                echo "Invalid -p $OPTARG" >&2
                exit 1;
            fi
            ;;
	P)
	    LOGFAC="$OPTARG"
	    ;;
        s)
            LEAPSRC="$OPTARG"
            ;;
        e)
            PREFETCH="$OPTARG"
            ;;
	f)
	    NTPCONF="$OPTARG"
	    ;;
        F)
            FORCE="Y"
            ;;
        c)
            RESTART="$OPTARG"
            ;;
        r)
            MAXTRIES="$OPTARG"
            ;;
        i)
            INTERVAL="$OPTARG"
            ;;
        t)
            TMPFILE="$OPTARG"
            ;;
	l)
	    SYSLOG="y"
	    ;;
	L)
	    SYSLOG=
	    ;;
        h)
            displayHelp
            exit 0
            ;;
	q)
	    QUIET="Y"
	    ;;
        v)
            VERBOSE="Y"
            ;;
	z)
	    PATHLIST="$OPTARG:"
	    ;;
	Z)
	    PATHLIST=
	    ;;
        *)
            echo "$SELF -h for usage" >&2
            exit 1
            ;;
    esac
done
shift $((OPTIND-1))

export PATH="$PATHLIST$PATH"

# Add to path to deal with embedded systems
#
for P in $REQUIREDCMDS ; do
    if >/dev/null 2>&1 which "$P" ; then
	continue
    fi
    [ "$P" = "logger" ] && continue
    echo "FATAL: missing $P command, please install"
    exit 1
done

# Handle logging

if ! LOGGER="`2>/dev/null which logger`" ; then
    LOGGER=
fi

function log {
    # "priority" "message"
    #
    # Stdout unless syslog specified or logger isn't available
    #
    if [ -z "$SYSLOG" -o -z "$LOGGER" ]; then
	if [ -n "$QUIET" -a \( "$1" = "info" -o "$1" = "notice" -o "$1" = "debug" \) ]; then
	    return 0
	fi
	echo "`echo \"$1\" | tr a-z A-Z`: $2"
	return 0
    fi

    # Also log to stdout if cron job && notice or higher
    local S
    if [ -n "$CRONJOB" -a \( "$1" != "info" \) -a \( "$1" != "debug" \) ] || [ -n "$VERBOSE" ]; then
	S="-s"
    fi
    $LOGGER $S -t "$SELF[$$]" -p "$LOGFAC.$1" "$2"
}

# Verify interval
INTERVAL=$(( $INTERVAL *1 ))

# Validate a leap-seconds file checksum
#
# File format: (full description in files)
# # marks comments, except:
# #$ number : the NTP date of the last update
# #@ number : the NTP date that the file expires
# Date (seconds since 1900) leaps : leaps is the # of seconds to add for times >= Date
# Date lines have comments.
# #h hex hex hex hex hex is the SHA-1 checksum of the data & dates, excluding whitespace w/o leading zeroes

function verifySHA {

    if [ ! -f "$1" ]; then
        return 1
    fi

    # Remove comments, except those that are markers for last update, expires and hash

    local RAW="`sed $1 -e'/^\\([0-9]\\|#[\$@h]\)/!d' -e'/^#[\$@h]/!s/#.*\$//g'`"

    # Extract just the data, removing all whitespace

    local DATA="`echo \"$RAW\" | sed -e'/^#h/d' -e's/^#[\$@]//g' | tr -d '[:space:]'`"

    # Compute the SHA hash of the data, removing the marker and filename
    # Computed in binary mode, which shouldn't matter since whitespace has been removed
    # shasum comes in several flavors; a portable one is available in Perl (with Digest::SHA)

    local DSHA="`echo -n \"$DATA\" | shasum | sed -e's/[? *].*$//'`"

    # Extract the file's hash. Restore any leading zeroes in hash segments.

    # The sed [] includes a tab (\t) and space; #h is followed by a tab and space
    local FSHA="`echo \"$RAW\" | sed -e'/^#h/!d' -e's/^#h//' -e's/[ 	] */ 0x/g'`"
    FSHA=`printf '%08x%08x%08x%08x%08x' $FSHA`

    if [ -n "$FSHA" -a \( "$FSHA" = "$DSHA" \) ]; then
        if [ -n "$2" ]; then
            log "info" "Checksum of $1 validated"
        fi
    else
        log "error" "Checksum of $1 is invalid:"
	[ -z "$FSHA" ] && FSHA="(no checksum record found in file)"
        log "error" "EXPECTED: $FSHA"
        log "error" "COMPUTED: $DSHA"
        return 1
    fi

    # Check the expiration date, converting NTP epoch to Unix epoch used by date

    EXPIRES="`echo \"$RAW\" | sed -e'/^#@/!d' -e's/^#@//' | tr -d '[:space:]'`"
    EXPIRES="$(($EXPIRES - 2208988800 ))"

    if [ $EXPIRES -lt `date -u +%s` ]; then
        log "notice" "File expired on `date -u -d \"Jan 1, 1970 00:00:00 +0000 + $EXPIRES seconds\"`"
        return 2
    fi

}

# Verify ntp.conf

if ! [ -f "$NTPCONF" ]; then
    log "critical" "Missing ntp configuration $NTPCONF"
    exit 1
fi

# Parse ntp.conf for leapfile directive

LEAPFILE="`sed $NTPCONF -e'/^ *leapfile  *.*$/!d' -e's/^ *leapfile  *//'`"
if [ -z "$LEAPFILE" ]; then
    log "error" "$NTPCONF does not specify a leapfile"
fi

# Allow placing the file someplace else - testing

if [ -n "$1" ]; then
    if [ "$1" != "$LEAPFILE" ]; then
	log "notice" "Requested install to $1, but $NTPCONF specifies $LEAPFILE"
    fi
    LEAPFILE="$1"
fi

# Verify the current file
# If it is missing, doesn't validate or expired
# Or is expiring soon
#  Download a new one

if [ -n "$FORCE" ] || ! verifySHA $LEAPFILE "$VERBOSE" || [ $EXPIRES -lt `date -d "NOW + $PREFETCH" +%s` ] ; then
    TRY=0
    while true; do
        TRY=$(( $TRY + 1 ))
        if [ -n "$VERBOSE" ]; then
            log "info" "Attempting download from $LEAPSRC, try $TRY.."
        fi
        if wget $PROTO $PREFER -o ${TMPFILE}.log $LEAPSRC -O $TMPFILE ; then
            log "info" "Download of $LEAPSRC succeeded"
            if [ -n "$VERBOSE" ]; then
                cat ${TMPFILE}.log
            fi

            if ! verifySHA $TMPFILE "$VERBOSE" ; then
		# There is no point in retrying, as the file on the server is almost
		# certainly corrupt.

                log "warning" "Downloaded file $TMPFILE rejected -- saved for diagnosis"
                cat ${TMPFILE}.log
                rm -f ${TMPFILE}.log
                exit 1
            fi
            rm -f ${TMPFILE}.log

	    # Set correct permissions on temporary file

	    REFFILE="$LEAPFILE"
            if [ ! -f $LEAPFILE ]; then
		log "notice" "$LEAPFILE was missing, creating new copy - check permissions"
                touch $LEAPFILE
		# Can't copy permissions from old file, copy from NTPCONF instead
		REFFILE="$NTPCONF"
            fi
            chmod --reference $REFFILE $TMPFILE
            chown --reference $REFFILE $TMPFILE
	    ( which selinuxenabled && selinuxenabled && which chcon ) >/dev/null 2>&1
            if  [ $? == 0 ] ; then
                chcon --reference $REFFILE $TMPFILE
            fi

	    # Replace current file with validated new one

            if mv -f $TMPFILE $LEAPFILE ; then
                log "notice" "Installed new $LEAPFILE from $LEAPSRC"
            else
                log "error" "Install $TMPFILE => $LEAPFILE failed -- saved for diagnosis"
                exit 1
            fi

	    # Restart NTP (or whatever else is specified)

	    if [ -n "$RESTART" ]; then
		if [ -n "$VERBOSE" ]; then
		    log "info" "Attempting restart action: $RESTART"
		fi
		R="$( 2>&1 $RESTART )"
		if [ $? -eq 0 ]; then
		    log "notice" "Restart action succeeded"
		    if [ -n "$VERBOSE" -a -n "$R" ]; then
			log "info" "$R"
		    fi
		else
		    log "error" "Restart action failed"
		    if [ -n "$R" ]; then
			log "error" "$R"
		    fi
		    exit 2
		fi
	    fi
            exit 0
	fi

	# Failed to download.  See about trying again

        rm -f $TMPFILE
        if [ $TRY -ge $MAXTRIES ]; then
            break;
        fi
        if [ -n "$VERBOSE" ]; then
            cat ${TMPFILE}.log
            log "info" "Waiting $INTERVAL minutes before retrying..."
        fi
        sleep $(( $INTERVAL * 60))
    done

    # Failed and out of retries

    log "warning" "Download from $LEAPSRC failed after $TRY attempts"
    if [ -f ${TMPFILE}.log ]; then
        cat ${TMPFILE}.log
        rm -f ${TMPFILE}.log $TMPFILE
    fi
    exit 1
fi
log "info" "Not time to replace $LEAPFILE"

exit 0

# EOF