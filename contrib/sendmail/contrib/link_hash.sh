#!/bin/sh
##
## Copyright (c) 2000 Proofpoint, Inc. and its suppliers.
##       All rights reserved.
##
## $Id: link_hash.sh,v 1.3 2013-11-22 20:51:18 ca Exp $
##
#
# ln a certificate to its hash
#
SSL=openssl
if test $# -ge 1
then
  for i in $@
  do
  C=$i.pem
  test -f $C || C=$i
  if test -f $C
  then
    H=`$SSL x509 -noout -hash < $C`.0
    if test -h $H -o -f $H
    then
      echo link $H to $C exists
    else
      ln -s $C $H
    fi
  else
    echo "$0: cannot open $C"
    exit 2
  fi
  done
else
  echo "$0: missing name"
  exit 1
fi
exit 0
