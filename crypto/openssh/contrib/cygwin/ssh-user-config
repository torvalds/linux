#!/bin/bash
#
# ssh-user-config, Copyright 2000-2014 Red Hat Inc.
#
# This file is part of the Cygwin port of OpenSSH.
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   
# IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    
# OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    
# THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               

# ======================================================================
# Initialization
# ======================================================================
PROGNAME=$(basename -- $0)
_tdir=$(dirname -- $0)
PROGDIR=$(cd $_tdir && pwd)

CSIH_SCRIPT=/usr/share/csih/cygwin-service-installation-helper.sh

# Subdirectory where the new package is being installed
PREFIX=/usr

# Directory where the config files are stored
SYSCONFDIR=/etc

source ${CSIH_SCRIPT}

auto_passphrase="no"
passphrase=""
pwdhome=
with_passphrase=

# ======================================================================
# Routine: create_identity
#   optionally create identity of type argument in ~/.ssh
#   optionally add result to ~/.ssh/authorized_keys
# ======================================================================
create_identity() {
  local file="$1"
  local type="$2"
  local name="$3"
  if [ ! -f "${pwdhome}/.ssh/${file}" ]
  then
    if csih_request "Shall I create a ${name} identity file for you?"
    then
      csih_inform "Generating ${pwdhome}/.ssh/${file}"
      if [ "${with_passphrase}" = "yes" ]
      then
        ssh-keygen -t "${type}" -N "${passphrase}" -f "${pwdhome}/.ssh/${file}" > /dev/null
      else
        ssh-keygen -t "${type}" -f "${pwdhome}/.ssh/${file}" > /dev/null
      fi
      if csih_request "Do you want to use this identity to login to this machine?"
      then
        csih_inform "Adding to ${pwdhome}/.ssh/authorized_keys"
        cat "${pwdhome}/.ssh/${file}.pub" >> "${pwdhome}/.ssh/authorized_keys"
      fi
    fi
  fi
} # === End of create_ssh1_identity() === #
readonly -f create_identity

# ======================================================================
# Routine: check_user_homedir
#   Perform various checks on the user's home directory
# SETS GLOBAL VARIABLE:
#   pwdhome
# ======================================================================
check_user_homedir() {
  pwdhome=$(getent passwd $UID | awk -F: '{ print $6; }')
  if [ "X${pwdhome}" = "X" ]
  then
    csih_error_multi \
      "There is no home directory set for you in the account database." \
      'Setting $HOME is not sufficient!'
  fi
  
  if [ ! -d "${pwdhome}" ]
  then
    csih_error_multi \
      "${pwdhome} is set in the account database as your home directory" \
      'but it is not a valid directory. Cannot create user identity files.'
  fi
  
  # If home is the root dir, set home to empty string to avoid error messages
  # in subsequent parts of that script.
  if [ "X${pwdhome}" = "X/" ]
  then
    # But first raise a warning!
    csih_warning "Your home directory in the account database is set to root (/). This is not recommended!"
    if csih_request "Would you like to proceed anyway?"
    then
      pwdhome=''
    else
      csih_warning "Exiting. Configuration is not complete"
      exit 1
    fi
  fi
  
  if [ -d "${pwdhome}" -a -n "`chmod -c g-w,o-w "${pwdhome}"`" ]
  then
    echo
    csih_warning 'group and other have been revoked write permission to your home'
    csih_warning "directory ${pwdhome}."
    csih_warning 'This is required by OpenSSH to allow public key authentication using'
    csih_warning 'the key files stored in your .ssh subdirectory.'
    csih_warning 'Revert this change ONLY if you know what you are doing!'
    echo
  fi
} # === End of check_user_homedir() === #
readonly -f check_user_homedir

# ======================================================================
# Routine: check_user_dot_ssh_dir
#   Perform various checks on the ~/.ssh directory
# PREREQUISITE:
#   pwdhome -- check_user_homedir()
# ======================================================================
check_user_dot_ssh_dir() {
  if [ -e "${pwdhome}/.ssh" -a ! -d "${pwdhome}/.ssh" ]
  then
    csih_error "${pwdhome}/.ssh is existent but not a directory. Cannot create user identity files."
  fi
  
  if [ ! -e "${pwdhome}/.ssh" ]
  then
    mkdir "${pwdhome}/.ssh"
    if [ ! -e "${pwdhome}/.ssh" ]
    then
      csih_error "Creating users ${pwdhome}/.ssh directory failed"
    fi
  fi
} # === End of check_user_dot_ssh_dir() === #
readonly -f check_user_dot_ssh_dir

# ======================================================================
# Routine: fix_authorized_keys_perms
#   Corrects the permissions of ~/.ssh/authorized_keys
# PREREQUISITE:
#   pwdhome   -- check_user_homedir()
# ======================================================================
fix_authorized_keys_perms() {
  if [ -e "${pwdhome}/.ssh/authorized_keys" ]
  then
    setfacl -b "${pwdhome}/.ssh/authorized_keys" 2>/dev/null || echo -n
    if ! chmod u-x,g-wx,o-wx "${pwdhome}/.ssh/authorized_keys"
    then
      csih_warning "Setting correct permissions to ${pwdhome}/.ssh/authorized_keys"
      csih_warning "failed.  Please care for the correct permissions.  The minimum requirement"
      csih_warning "is, the owner needs read permissions."
      echo
    fi
  fi
} # === End of fix_authorized_keys_perms() === #
readonly -f fix_authorized_keys_perms


# ======================================================================
# Main Entry Point
# ======================================================================

# Check how the script has been started.  If
#   (1) it has been started by giving the full path and
#       that path is /etc/postinstall, OR
#   (2) Otherwise, if the environment variable
#       SSH_USER_CONFIG_AUTO_ANSWER_NO is set
# then set auto_answer to "no".  This allows automatic
# creation of the config files in /etc w/o overwriting
# them if they already exist.  In both cases, color
# escape sequences are suppressed, so as to prevent
# cluttering setup's logfiles.
if [ "$PROGDIR" = "/etc/postinstall" ]
then
  csih_auto_answer="no"
  csih_disable_color
fi
if [ -n "${SSH_USER_CONFIG_AUTO_ANSWER_NO}" ]
then
  csih_auto_answer="no"
  csih_disable_color
fi

# ======================================================================
# Parse options
# ======================================================================
while :
do
  case $# in
  0)
    break
    ;;
  esac

  option=$1
  shift

  case "$option" in
  -d | --debug )
    set -x
    csih_trace_on
    ;;

  -y | --yes )
    csih_auto_answer=yes
    ;;

  -n | --no )
    csih_auto_answer=no
    ;;

  -p | --passphrase )
    with_passphrase="yes"
    passphrase=$1
    shift
    ;;

  *)
    echo "usage: ${PROGNAME} [OPTION]..."
    echo
    echo "This script creates an OpenSSH user configuration."
    echo
    echo "Options:"
    echo "    --debug      -d        Enable shell's debug output."
    echo "    --yes        -y        Answer all questions with \"yes\" automatically."
    echo "    --no         -n        Answer all questions with \"no\" automatically."
    echo "    --passphrase -p word   Use \"word\" as passphrase automatically."
    echo
    exit 1
    ;;

  esac
done

# ======================================================================
# Action!
# ======================================================================

check_user_homedir
check_user_dot_ssh_dir
create_identity id_rsa rsa "SSH2 RSA"
create_identity id_dsa dsa "SSH2 DSA"
create_identity id_ecdsa ecdsa "SSH2 ECDSA"
create_identity identity rsa1 "(deprecated) SSH1 RSA"
fix_authorized_keys_perms

echo
csih_inform "Configuration finished. Have fun!"


