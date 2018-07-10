#!/bin/sh

gcc -Wall -Os -o hardshutdown hardshutdown.c
strip hardshutdown

#or:
#diet gcc -Wall -o hardshutdown hardshutdown.c
#elftrunc hardshutdown
