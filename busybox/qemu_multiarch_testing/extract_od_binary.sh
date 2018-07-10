#!/bin/sh

# Converts textual result of "od -tx1 <FILE"
# back into a binary FILE

grep -a '^[0-7][0-7][0-7][0-7][0-7][0-7][0-7][0-7]* [0-9a-f][0-9a-f] [0-9a-f][0-9a-f] [0-9a-f][0-9a-f] [0-9a-f][0-9a-f]' | busybox hexdump -R
