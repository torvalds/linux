#!/bin/sh
# $Id: msgbox4-eucjp,v 1.5 2010/01/13 10:20:03 tom Exp $

. ./setup-vars

width=35
while test $width != 61
do
$DIALOG --title "MESSAGE BOX (width $width)" --clear --no-collapse "$@" \
        --msgbox "\
This sample is written in EUC-JP.
There are several checking points:
(1) whether the fullwidth characters are displayed well or not,
(2) whether the width of characters are evaluated properly, and
(3) whether the character at line-folding is lost or not.

あいうえおかきくけこさしすせそたちつてとなにぬねの
１２３４５６７８９０１２３４５６７８９０１２３４５
ＡＢＣＤＥＦＧＨＩＪＫＬＭＮＯＰＱＲＳＴＵＶＷＸＹ

Hi, this is a simple message box.  You can use this to \
display any message you like.  The box will remain until \
you press the ENTER key." 22 $width
width=`expr $width + 1`
done
