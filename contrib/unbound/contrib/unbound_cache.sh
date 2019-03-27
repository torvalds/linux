#!/sbin/sh

# --------------------------------------------------------------
# -- DNS cache save/load script
# --
# -- Version 1.2
# -- By Yuri Voinov (c) 2006, 2014
# --------------------------------------------------------------
#
# ident   "@(#)unbound_cache.sh     1.2     14/10/30 YV"
#

#############
# Variables #
#############

# Installation base dir
CONF="/etc/opt/csw/unbound"
BASE="/opt/csw"

# Unbound binaries
UC="$BASE/sbin/unbound-control"
FNAME="unbound_cache.dmp"

# OS utilities
BASENAME=`which basename`
CAT=`which cat`
CUT=`which cut`
ECHO=`which echo`
EXPR=`which expr`
GETOPT=`which getopt`
ID=`which id`
LS=`which ls`

###############
# Subroutines #
###############

usage_note ()
{
# Script usage note
 $ECHO "Usage: `$BASENAME $0` [-s] or [-l] or [-r] or [-h] [filename]"
 $ECHO .
 $ECHO "l - Load - default mode. Warming up Unbound DNS cache from saved file. cache-ttl must be high value."
 $ECHO "s - Save - save Unbound DNS cache contents to plain file with domain names."
 $ECHO "r - Reload - reloadind new cache entries and refresh existing cache"
 $ECHO "h - this screen."
 $ECHO "filename - file to save/load dumped cache. If not specified, $CONF/$FNAME will be used instead."
 $ECHO "Note: Run without any arguments will be in default mode."
 $ECHO "      Also, unbound-control must be configured."
 exit 0
}

root_check ()
{
 if [ ! `$ID | $CUT -f1 -d" "` = "uid=0(root)" ]; then
  $ECHO "ERROR: You must be super-user to run this script."
  exit 1
 fi
}

check_uc ()
{
 if [ ! -f "$UC" ]; then
  $ECHO .
  $ECHO "ERROR: $UC not found. Exiting..."
  exit 1
 fi
}

check_saved_file ()
{
 filename=$1
 if [ ! -z "$filename" -a ! -f "$filename" ]; then
  $ECHO .
  $ECHO "ERROR: File $filename does not exists. Save it first."
  exit 1
 elif [ ! -f "$CONF/$FNAME" ]; then
  $ECHO .
  $ECHO "ERROR: File $CONF/$FNAME does not exists. Save it first."
  exit 1
 fi
}

save_cache ()
{
 # Save unbound cache
 filename=$1
 if [ -z "$filename" ]; then
  $ECHO "Saving cache in $CONF/$FNAME..."
  $UC dump_cache>$CONF/$FNAME
  $LS -lh $CONF/$FNAME
 else
  $ECHO "Saving cache in $filename..."
  $UC dump_cache>$filename
  $LS -lh $filename
 fi
 $ECHO "ok"
}

load_cache ()
{
 # Load saved cache contents and warmup cache
 filename=$1
 if [ -z "$filename" ]; then
  $ECHO "Loading cache from saved $CONF/$FNAME..."
  $LS -lh $CONF/$FNAME
  check_saved_file $filename
  $CAT $CONF/$FNAME|$UC load_cache
 else
  $ECHO "Loading cache from saved $filename..."
  $LS -lh $filename
  check_saved_file $filename
  $CAT $filename|$UC load_cache
 fi
}

reload_cache ()
{
 # Reloading and refresh existing cache and saved dump
 filename=$1
 save_cache $filename
 load_cache $filename
}

##############
# Main block #
##############

# Root check
root_check

# Check unbound-control
check_uc

# Check command-line arguments
if [ "x$*" = "x" ]; then
 # If arguments list empty,load cache by default
 load_cache
else
 arg_list=$*
 # Parse command line
 set -- `$GETOPT sSlLrRhH: $arg_list` || {
  usage_note 1>&2
 }

 # Read arguments
 for i in $arg_list
  do
   case $i in
    -s | -S) save="1";;
    -l | -L) save="0";;
    -r | -R) save="2";;
    -h | -H | \?) usage_note;;
    *) shift
       file=$1
       break;;
   esac
   shift
  done

 # Remove trailing --
 shift `$EXPR $OPTIND - 1`
fi

if [ "$save" = "1" ]; then
 save_cache $file
elif [ "$save" = "0" ]; then
 load_cache $file
elif [ "$save" = "2" ]; then
 reload_cache $file
fi

exit 0