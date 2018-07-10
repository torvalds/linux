#!/bin/sh

# Wrapper for User Mode Linux emulation environment

RUNFILE="$(pwd)/${1}.testroot"
if [ -z "$RUNFILE" ] || [ ! -x "$RUNFILE" ]
then
  echo "Can't run '$RUNFILE'"
  exit 1
fi

shift

if [ -z $(which linux) ]
then
  echo "No User Mode Linux."
  exit 1;
fi

linux rootfstype=hostfs rw init="$RUNFILE" TESTDIR=`pwd` PATH="$PATH" $* quiet
