#!/bin/sh
# Generate arm-tune.md, a file containing the tune attribute from the list of 
# CPUs in arm-cores.def

echo ";; -*- buffer-read-only: t -*-"
echo ";; Generated automatically by gentune.sh from arm-cores.def"

allcores=`awk -F'[(, 	]+' '/^ARM_CORE/ { cores = cores$3"," } END { print cores } ' $1`

echo "(define_attr \"tune\""
echo "	\"$allcores\"" | sed -e 's/,"$/"/'
echo "	(const (symbol_ref \"arm_tune\")))"
