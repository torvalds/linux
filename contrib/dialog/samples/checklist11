#! /bin/sh
# $Id: checklist11,v 1.1 2010/01/17 23:04:01 tom Exp $

. ./setup-vars

. ./setup-tempfile

$DIALOG --backtitle "No Such Organization" \
	--title "CHECKLIST BOX" "$@" \
        --checklist "Hi, this is a checklist box. You can use this to \n\
present a list of choices which can be turned on or \n\
off. If there are more items than can fit on the \n\
screen, the list will be scrolled. You can use the \n\
UP/DOWN arrow keys, the first letter of the choice as a \n\
hot key, or the number keys 1-9 to choose an option. \n\
Press SPACE to toggle an option on/off. \n\n\
  Which of the following are fruits?" 20 61 5 \
        "" "It's an apple." off \
        "Dog"    "No1, that's not my dog." ON \
        "Dog2"   "No2, that's not my dog." OFF \
        "Dog3"   "No3, that's not my dog." OFF \
        "Dog4"   "No4, that's not my dog." OFF \
        "Dog5"   "No5, that's not my dog." OFF \
        "Dog6"   "No6, that's not my dog." OFF \
        "Orange" "Yeah, that's juicy." off \
        "Chicken"    "" off \
        "Cat"    "No, never put a dog and a cat together!" oN \
        "Fish"   "Cats like fish." On \
        "Lemon"  "You know how it tastes." on 2> $tempfile

retval=$?

. ./report-tempfile
