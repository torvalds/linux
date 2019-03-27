#!/usr/bin/python2
#
# Post-process the output of test-dumpevents and check it for correctness.
#

import math
import re
import sys

text = sys.stdin.readlines()

try:
    expect_inserted_pos = text.index("Inserted:\n")
    expect_active_pos = text.index("Active:\n")
    got_inserted_pos = text.index("Inserted events:\n")
    got_active_pos = text.index("Active events:\n")
except ValueError:
    print >>sys.stderr, "Missing expected dividing line in dumpevents output"
    sys.exit(1)

if not (expect_inserted_pos < expect_active_pos <
        got_inserted_pos < got_active_pos):
    print >>sys.stderr, "Sections out of order in dumpevents output"
    sys.exit(1)

now,T= text[1].split()
T = float(T)

want_inserted = set(text[expect_inserted_pos+1:expect_active_pos])
want_active = set(text[expect_active_pos+1:got_inserted_pos-1])
got_inserted = set(text[got_inserted_pos+1:got_active_pos])
got_active = set(text[got_active_pos+1:])

pat = re.compile(r'Timeout=([0-9\.]+)')
def replace_time(m):
    t = float(m.group(1))
    if .9 < abs(t-T) < 1.1:
        return "Timeout=T+1"
    elif 2.4 < abs(t-T) < 2.6:
        return "Timeout=T+2.5"
    else:
        return m.group(0)

cleaned_inserted = set( pat.sub(replace_time, s) for s in got_inserted
                        if "Internal" not in s)

if cleaned_inserted != want_inserted:
    print >>sys.stderr, "Inserted event lists were not as expected!"
    sys.exit(1)

if set(got_active) != set(want_active):
    print >>sys.stderr, "Active event lists were not as expected!"
    sys.exit(1)

