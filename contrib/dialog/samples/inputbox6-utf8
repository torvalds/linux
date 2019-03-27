#!/bin/sh
# $Id: inputbox6-utf8,v 1.9 2013/09/24 00:06:02 tom Exp $

. ./setup-vars

. ./setup-tempfile

. ./setup-utf8

TITLE="あいうえお"

$DIALOG \
--title    "$TITLE" "$@" \
--inputbox "$TITLE" 10 20 "Ｄ.Ｏ.Ｇ"	 2>$tempfile

retval=$?

. ./report-tempfile
