#! /bin/sh
# $Id: radiolist2,v 1.8 2010/01/13 10:20:03 tom Exp $

. ./setup-vars

. ./setup-tempfile

$DIALOG --ok-label Okay \
	--cancel-label 'Give Up' \
	--backtitle "No Such Organization" \
	--title "RADIOLIST BOX" --clear "$@" \
        --radiolist "Hi, this is a radiolist box. You can use this to \n\
present a list of choices which can be turned on or \n\
off. If there are more items than can fit on the \n\
screen, the list will be scrolled. You can use the \n\
UP/DOWN arrow keys, the first letter of the choice as a \n\
hot key, or the number keys 1-9 to choose an option. \n\
Press SPACE to toggle an option on/off. \n\n\
  Which of the following are fruits?" 20 61 5 \
        "Apple"  "It's an apple." off \
        "Dog"    "No, that's not my dog." ON \
        "Orange" "Yeah, that's juicy." off \
        "Chicken"    "Normally not a pet." off \
        "Cat"    "No, never put a dog and a cat together!" off \
        "Fish"   "Cats like fish." off \
        "Lemon"  "You know how it tastes." off 2> $tempfile

retval=$?

. ./report-tempfile
