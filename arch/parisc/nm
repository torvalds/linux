#!/bin/sh
##
# Hack to have an nm which removes the local symbols.  We also rely
# on this nm being hidden out of the ordinarily executable path
##
${CROSS_COMPILE}nm $* | grep -v '.LC*[0-9]*$'
