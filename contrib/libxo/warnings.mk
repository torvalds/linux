#
# $Id$
#
# Copyright 2011, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#
# Commonly used sets of warnings
#

MIN_WARNINGS?= -W -Wall

LOW_WARNINGS?= ${MIN_WARNINGS} \
    -Wstrict-prototypes \
    -Wmissing-prototypes \
    -Wpointer-arith

MEDIUM_WARNINGS?= ${LOW_WARNINGS} -Werror

HIGH_WARNINGS?= ${MEDIUM_WARNINGS} \
    -Waggregate-return \
    -Wcast-align \
    -Wcast-qual \
    -Wchar-subscripts \
    -Wcomment \
    -Wformat \
    -Wimplicit \
    -Wmissing-declarations \
    -Wnested-externs \
    -Wparentheses \
    -Wreturn-type \
    -Wshadow \
    -Wswitch \
    -Wtrigraphs \
    -Wuninitialized \
    -Wunused \
    -Wwrite-strings

HIGHER_WARNINGS?= ${HIGH_WARNINGS} \
    -Winline \
    -Wbad-function-cast \
    -Wpacked \
    -Wpadded \
    -Wstrict-aliasing

ifeq "${LIBXO_WARNINGS}" "HIGH"
WARNINGS += ${HIGH_WARNINGS}
else
WARNINGS += ${LOW_WARNINGS}
endif

ifeq "${GCC_WARNINGS}" "yes"
WARNINGS += -fno-inline-functions-called-once
endif
