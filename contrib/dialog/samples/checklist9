#! /bin/sh
# $Id: checklist9,v 1.8 2010/01/13 10:20:03 tom Exp $
# "checklist8" using --file

. ./setup-vars

. ./setup-tempfile

$DIALOG "$@" --file checklist9.txt 2>$tempfile

retval=$?

. ./report-tempfile
