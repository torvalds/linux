#!/bin/bash

function usage() {
  echo "Usage: $0 INPUT... OUTPUT"
  exit 1
}

if [ "$#" -le 1 ]; then
  usage
fi

AR=$(readlink -f $AR)
LINK=$(readlink -f $LINK)

INPUTS=
OUTPUT=
for ARG in $@; do
  INPUTS="$INPUTS $OUTPUT"
  OUTPUT=$(readlink -f $ARG)
done

echo Inputs: $INPUTS
echo Output: $OUTPUT

SCRATCH_DIR=$(mktemp -d)
ln -s $INPUTS $SCRATCH_DIR/

pushd $SCRATCH_DIR

for INPUT in *; do
  for OBJ in $($AR t $INPUT); do
    $AR x $INPUT $OBJ
    mv -f $OBJ $(basename $INPUT).$OBJ
  done
done

$LINK *.o -o $OUTPUT

rm -rf $SCRATCH_DIR
