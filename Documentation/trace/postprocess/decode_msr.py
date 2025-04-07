#!/usr/bin/env python
# add symbolic names to read_msr / write_msr in trace
# decode_msr msr-index.h < trace
import sys
import re

msrs = dict()

with open(sys.argv[1] if len(sys.argv) > 1 else "msr-index.h", "r") as f:
	for j in f:
		m = re.match(r'#define (MSR_\w+)\s+(0x[0-9a-fA-F]+)', j)
		if m:
			msrs[int(m.group(2), 16)] = m.group(1)

extra_ranges = (
	( "MSR_LASTBRANCH_%d_FROM_IP", 0x680, 0x69F ),
	( "MSR_LASTBRANCH_%d_TO_IP", 0x6C0, 0x6DF ),
	( "LBR_INFO_%d", 0xdc0, 0xddf ),
)

for j in sys.stdin:
	m = re.search(r'(read|write)_msr:\s+([0-9a-f]+)', j)
	if m:
		r = None
		num = int(m.group(2), 16)
		if num in msrs:
			r = msrs[num]
		else:
			for er in extra_ranges:
				if er[1] <= num <= er[2]:
					r = er[0] % (num - er[1],)
					break
		if r:
			j = j.replace(" " + m.group(2), " " + r + "(" + m.group(2) + ")")
	print(j)


