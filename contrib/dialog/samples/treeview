#!/bin/sh
# $Id: treeview,v 1.2 2012/12/04 10:53:09 tom Exp $

. ./setup-vars

. ./setup-tempfile

$DIALOG --title "TREE VIEW DIALOG" \
	--treeview "TreeView demo" 0 0 0 \
		tag1 one off 0 \
		tag2 two off 1 \
		tag3 three on 2 \
		tag4 four off 1 \
		tag5 five off 2 \
		tag6 six off 3 \
		tag7 seven off 3 \
		tag8 eight off 4 \
		tag9 nine off 1 2> $tempfile

retval=$?

. ./report-tempfile
