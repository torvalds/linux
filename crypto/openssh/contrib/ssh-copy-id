#!/bin/sh

# Copyright (c) 1999-2016 Philip Hands <phil@hands.com>
#               2013 Martin Kletzander <mkletzan@redhat.com>
#               2010 Adeodato =?iso-8859-1?Q?Sim=F3?= <asp16@alu.ua.es>
#               2010 Eric Moret <eric.moret@gmail.com>
#               2009 Xr <xr@i-jeuxvideo.com>
#               2007 Justin Pryzby <justinpryzby@users.sourceforge.net>
#               2004 Reini Urban <rurban@x-ray.at>
#               2003 Colin Watson <cjwatson@debian.org>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Shell script to install your public key(s) on a remote machine
# See the ssh-copy-id(1) man page for details

# check that we have something mildly sane as our shell, or try to find something better
if false ^ printf "%s: WARNING: ancient shell, hunting for a more modern one... " "$0"
then
  SANE_SH=${SANE_SH:-/usr/bin/ksh}
  if printf 'true ^ false\n' | "$SANE_SH"
  then
    printf "'%s' seems viable.\n" "$SANE_SH"
    exec "$SANE_SH" "$0" "$@"
  else
    cat <<-EOF
	oh dear.

	  If you have a more recent shell available, that supports \$(...) etc.
	  please try setting the environment variable SANE_SH to the path of that
	  shell, and then retry running this script. If that works, please report
	  a bug describing your setup, and the shell you used to make it work.

	EOF
    printf "%s: ERROR: Less dimwitted shell required.\n" "$0"
    exit 1
  fi
fi

most_recent_id="$(cd "$HOME" ; ls -t .ssh/id*.pub 2>/dev/null | grep -v -- '-cert.pub$' | head -n 1)"
DEFAULT_PUB_ID_FILE="${most_recent_id:+$HOME/}$most_recent_id"

usage () {
  printf 'Usage: %s [-h|-?|-f|-n] [-i [identity_file]] [-p port] [[-o <ssh -o options>] ...] [user@]hostname\n' "$0" >&2
  printf '\t-f: force mode -- copy keys without trying to check if they are already installed\n' >&2
  printf '\t-n: dry run    -- no keys are actually copied\n' >&2
  printf '\t-h|-?: print this help\n' >&2
  exit 1
}

# escape any single quotes in an argument
quote() {
  printf "%s\n" "$1" | sed -e "s/'/'\\\\''/g"
}

use_id_file() {
  local L_ID_FILE="$1"

  if [ -z "$L_ID_FILE" ] ; then
    printf "%s: ERROR: no ID file found\n" "$0"
    exit 1
  fi

  if expr "$L_ID_FILE" : ".*\.pub$" >/dev/null ; then
    PUB_ID_FILE="$L_ID_FILE"
  else
    PUB_ID_FILE="$L_ID_FILE.pub"
  fi

  [ "$FORCED" ] || PRIV_ID_FILE=$(dirname "$PUB_ID_FILE")/$(basename "$PUB_ID_FILE" .pub)

  # check that the files are readable
  for f in "$PUB_ID_FILE" ${PRIV_ID_FILE:+"$PRIV_ID_FILE"} ; do
    ErrMSG=$( { : < "$f" ; } 2>&1 ) || {
      local L_PRIVMSG=""
      [ "$f" = "$PRIV_ID_FILE" ] && L_PRIVMSG="	(to install the contents of '$PUB_ID_FILE' anyway, look at the -f option)"
      printf "\n%s: ERROR: failed to open ID file '%s': %s\n" "$0" "$f" "$(printf "%s\n%s\n" "$ErrMSG" "$L_PRIVMSG" | sed -e 's/.*: *//')"
      exit 1
    }
  done
  printf '%s: INFO: Source of key(s) to be installed: "%s"\n' "$0" "$PUB_ID_FILE" >&2
  GET_ID="cat \"$PUB_ID_FILE\""
}

if [ -n "$SSH_AUTH_SOCK" ] && ssh-add -L >/dev/null 2>&1 ; then
  GET_ID="ssh-add -L"
fi

while test "$#" -gt 0
do
  [ "${SEEN_OPT_I}" ] && expr "$1" : "[-]i" >/dev/null && {
        printf "\n%s: ERROR: -i option must not be specified more than once\n\n" "$0"
        usage
  }

  OPT= OPTARG=
  # implement something like getopt to avoid Solaris pain
  case "$1" in
    -i?*|-o?*|-p?*)
      OPT="$(printf -- "$1"|cut -c1-2)"
      OPTARG="$(printf -- "$1"|cut -c3-)"
      shift
      ;;
    -o|-p)
      OPT="$1"
      OPTARG="$2"
      shift 2
      ;;
    -i)
      OPT="$1"
      test "$#" -le 2 || expr "$2" : "[-]" >/dev/null || {
        OPTARG="$2"
        shift
      }
      shift
      ;;
    -f|-n|-h|-\?)
      OPT="$1"
      OPTARG=
      shift
      ;;
    --)
      shift
      while test "$#" -gt 0
      do
        SAVEARGS="${SAVEARGS:+$SAVEARGS }'$(quote "$1")'"
        shift
      done
      break
      ;;
    -*)
      printf "\n%s: ERROR: invalid option (%s)\n\n" "$0" "$1"
      usage
      ;;
    *)
      SAVEARGS="${SAVEARGS:+$SAVEARGS }'$(quote "$1")'"
      shift
      continue
      ;;
  esac

  case "$OPT" in
    -i)
      SEEN_OPT_I="yes"
      use_id_file "${OPTARG:-$DEFAULT_PUB_ID_FILE}"
      ;;
    -o|-p)
      SSH_OPTS="${SSH_OPTS:+$SSH_OPTS }$OPT '$(quote "$OPTARG")'"
      ;;
    -f)
      FORCED=1
      ;;
    -n)
      DRY_RUN=1
      ;;
    -h|-\?)
      usage
      ;;
  esac
done 

eval set -- "$SAVEARGS"

if [ $# = 0 ] ; then
  usage
fi
if [ $# != 1 ] ; then
  printf '%s: ERROR: Too many arguments.  Expecting a target hostname, got: %s\n\n' "$0" "$SAVEARGS" >&2
  usage
fi

# drop trailing colon
USER_HOST=$(printf "%s\n" "$1" | sed 's/:$//')
# tack the hostname onto SSH_OPTS
SSH_OPTS="${SSH_OPTS:+$SSH_OPTS }'$(quote "$USER_HOST")'"
# and populate "$@" for later use (only way to get proper quoting of options)
eval set -- "$SSH_OPTS"

if [ -z "$(eval $GET_ID)" ] && [ -r "${PUB_ID_FILE:=$DEFAULT_PUB_ID_FILE}" ] ; then
  use_id_file "$PUB_ID_FILE"
fi

if [ -z "$(eval $GET_ID)" ] ; then
  printf '%s: ERROR: No identities found\n' "$0" >&2
  exit 1
fi

# populate_new_ids() uses several global variables ($USER_HOST, $SSH_OPTS ...)
# and has the side effect of setting $NEW_IDS
populate_new_ids() {
  local L_SUCCESS="$1"

  if [ "$FORCED" ] ; then
    NEW_IDS=$(eval $GET_ID)
    return
  fi

  # repopulate "$@" inside this function 
  eval set -- "$SSH_OPTS"

  umask 0177
  local L_TMP_ID_FILE=$(mktemp ~/.ssh/ssh-copy-id_id.XXXXXXXXXX)
  if test $? -ne 0 || test "x$L_TMP_ID_FILE" = "x" ; then
    printf '%s: ERROR: mktemp failed\n' "$0" >&2
    exit 1
  fi
  local L_CLEANUP="rm -f \"$L_TMP_ID_FILE\" \"${L_TMP_ID_FILE}.stderr\""
  trap "$L_CLEANUP" EXIT TERM INT QUIT
  printf '%s: INFO: attempting to log in with the new key(s), to filter out any that are already installed\n' "$0" >&2
  NEW_IDS=$(
    eval $GET_ID | {
      while read ID || [ "$ID" ] ; do
        printf '%s\n' "$ID" > "$L_TMP_ID_FILE"

        # the next line assumes $PRIV_ID_FILE only set if using a single id file - this
        # assumption will break if we implement the possibility of multiple -i options.
        # The point being that if file based, ssh needs the private key, which it cannot
        # find if only given the contents of the .pub file in an unrelated tmpfile
        ssh -i "${PRIV_ID_FILE:-$L_TMP_ID_FILE}" \
            -o ControlPath=none \
            -o LogLevel=INFO \
            -o PreferredAuthentications=publickey \
            -o IdentitiesOnly=yes "$@" exit 2>"$L_TMP_ID_FILE.stderr" </dev/null
        if [ "$?" = "$L_SUCCESS" ] ; then
          : > "$L_TMP_ID_FILE"
        else
          grep 'Permission denied' "$L_TMP_ID_FILE.stderr" >/dev/null || {
            sed -e 's/^/ERROR: /' <"$L_TMP_ID_FILE.stderr" >"$L_TMP_ID_FILE"
            cat >/dev/null #consume the other keys, causing loop to end
          }
        fi

        cat "$L_TMP_ID_FILE"
      done
    }
  )
  eval "$L_CLEANUP" && trap - EXIT TERM INT QUIT

  if expr "$NEW_IDS" : "^ERROR: " >/dev/null ; then
    printf '\n%s: %s\n\n' "$0" "$NEW_IDS" >&2
    exit 1
  fi
  if [ -z "$NEW_IDS" ] ; then
    printf '\n%s: WARNING: All keys were skipped because they already exist on the remote system.\n' "$0" >&2
    printf '\t\t(if you think this is a mistake, you may want to use -f option)\n\n' "$0" >&2
    exit 0
  fi
  printf '%s: INFO: %d key(s) remain to be installed -- if you are prompted now it is to install the new keys\n' "$0" "$(printf '%s\n' "$NEW_IDS" | wc -l)" >&2
}

REMOTE_VERSION=$(ssh -v -o PreferredAuthentications=',' -o ControlPath=none "$@" 2>&1 |
                 sed -ne 's/.*remote software version //p')

case "$REMOTE_VERSION" in
  NetScreen*)
    populate_new_ids 1
    for KEY in $(printf "%s" "$NEW_IDS" | cut -d' ' -f2) ; do
      KEY_NO=$(($KEY_NO + 1))
      printf "%s\n" "$KEY" | grep ssh-dss >/dev/null || {
         printf '%s: WARNING: Non-dsa key (#%d) skipped (NetScreen only supports DSA keys)\n' "$0" "$KEY_NO" >&2
         continue
      }
      [ "$DRY_RUN" ] || printf 'set ssh pka-dsa key %s\nsave\nexit\n' "$KEY" | ssh -T "$@" >/dev/null 2>&1
      if [ $? = 255 ] ; then
        printf '%s: ERROR: installation of key #%d failed (please report a bug describing what caused this, so that we can make this message useful)\n' "$0" "$KEY_NO" >&2
      else
        ADDED=$(($ADDED + 1))
      fi
    done
    if [ -z "$ADDED" ] ; then
      exit 1
    fi
    ;;
  *)
    # Assuming that the remote host treats ~/.ssh/authorized_keys as one might expect
    populate_new_ids 0
    # in ssh below - to defend against quirky remote shells: use 'exec sh -c' to get POSIX;
    #     'cd' to be at $HOME; add a newline if it's missing; and all on one line, because tcsh.
    [ "$DRY_RUN" ] || printf '%s\n' "$NEW_IDS" | \
      ssh "$@" "exec sh -c 'cd ; umask 077 ; mkdir -p .ssh && { [ -z "'`tail -1c .ssh/authorized_keys 2>/dev/null`'" ] || echo >> .ssh/authorized_keys ; } && cat >> .ssh/authorized_keys || exit 1 ; if type restorecon >/dev/null 2>&1 ; then restorecon -F .ssh .ssh/authorized_keys ; fi'" \
      || exit 1
    ADDED=$(printf '%s\n' "$NEW_IDS" | wc -l)
    ;;
esac

if [ "$DRY_RUN" ] ; then
  cat <<-EOF
	=-=-=-=-=-=-=-=
	Would have added the following key(s):

	$NEW_IDS
	=-=-=-=-=-=-=-=
	EOF
else
  cat <<-EOF

	Number of key(s) added: $ADDED

	Now try logging into the machine, with:   "ssh $SSH_OPTS"
	and check to make sure that only the key(s) you wanted were added.

	EOF
fi

# =-=-=-=
