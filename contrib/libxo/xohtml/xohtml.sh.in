#!/bin/sh
#
# Copyright (c) 2014, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
# Phil Shafer, July 2014
#

BASE=@XO_SHAREDIR@
VERSION=@LIBXO_VERSION@
CMD=cat
DONE=
WEB=http://juniper.github.io/libxo/${VERSION}/xohtml

do_help () {
    echo "xohtml: wrap libxo-enabled output in HTML"
    echo "Usage: xohtml [options] [command [arguments]]"
    echo "Valid options are:"
    echo "    -b <basepath> | --base <basepath>"
    echo "    -c <command> | --command <command>"
    echo "    -f <output-file> | --file <output-file>"
    exit 1
}

while [ -z "$DONE" -a ! -z "$1" ]; do
    case "$1" in
        -b|--base)
            shift;
            BASE="$1";
	    shift;
            ;;
        -c|--command)
            shift;
            CMD="$1";
	    shift;
            ;;
        -f|--file)
            shift;
            FILE="$1";
	    shift;
	    exec > "$FILE";
            ;;
        -w|--web)
            shift;
            BASE="${WEB}";
            ;;

	-*)
	    do_help
	    ;;
	*)
	    DONE=1;
	    XX=$1;
	    shift;
	    CMD="$XX --libxo=html $@"
	    ;;
    esac
done

if [ "$CMD" = "cat" -a -t 0 ]; then
    do_help
fi

echo '<html>'
echo '<head>'
echo '<meta http-equiv="content-type" content="text/html; charset=utf-8"/>'
echo '<link rel="stylesheet" href="'$BASE'/xohtml.css">'
echo '<link rel="stylesheet" href="'$BASE'/external/jquery.qtip.css"/>'
echo '<script type="text/javascript" src="'$BASE'/external/jquery.js"></script>'
echo '<script type="text/javascript" src="'$BASE'/external/jquery.qtip.js"></script>'
echo '<script type="text/javascript" src="'$BASE'/xohtml.js"></script>'
echo '<script>'
echo '</script>'
echo '</head>'
echo '<body>'

$CMD

echo '</body>'
echo '</html>'

exit 0
