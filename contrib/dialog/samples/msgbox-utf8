#!/bin/sh
# $Id: msgbox-utf8,v 1.1 2011/01/18 00:25:30 tom Exp $
# from Debian #570634

. ./setup-vars

. ./setup-utf8

${DIALOG-dialog} "$@" \
	--title "ทดสอบวรรณยุกต์" \
	--msgbox "วรรณยุกต์อยู่ท้ายบรรทัดได้หรือไม่" 8 23
retval=$?

. ./report-button
