#!/bin/sh

# Sample input filter to print on HP Laser Jet printers
# Installed in /usr/local/libexec/hp6l

DEVICE="ljet3"
PAPERSIZE="a4"

printf "\033&k2G" || exit 2

read first_line
first_two_chars=`expr "$first_line" : '\(..\)'`

if [ "$first_two_chars" = "%!" ]; then
        exec 3>&1 1>&2
        /usr/local/bin/gs -sPAPERSIZE=${PAPERSIZE} -dSAFER -dNOPAUSE -q -sDEVICE=${DEVICE} \
            -sOutputFile=/dev/fd/3 -  && exit 0
else
        echo $first_line && cat && printf "\033&l0H" && exit 0
fi

exit 2
