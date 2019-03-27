#!/usr/bin/env python
#
# Copyright (c) 2012, Intel Corporation
#
# Author: Johannes Berg <johannes@sipsolutions.net>
#
# This software may be distributed under the terms of the BSD license.
# See README for more details.

import sys, struct, re

def write_pcap_header(pcap_file):
    pcap_file.write(
        struct.pack('<IHHIIII',
                    0xa1b2c3d4, 2, 4, 0, 0, 65535,
                    105 # raw 802.11 format
                    ))

def pcap_addpacket(pcap_file, ts, data):
    # ts in seconds, float
    pcap_file.write(struct.pack('<IIII',
        int(ts), int(1000000 * ts) % 1000000,
        len(data), len(data)))
    pcap_file.write(data)

if __name__ == "__main__":
    try:
        input = sys.argv[1]
        pcap = sys.argv[2]
    except IndexError:
        print "Usage: %s <log file> <pcap file>" % sys.argv[0]
        sys.exit(2)

    input_file = open(input, 'r')
    pcap_file = open(pcap, 'w')
    frame_re = re.compile(r'(([0-9]+.[0-9]{6}):\s*)?nl80211: MLME event frame - hexdump\(len=[0-9]*\):((\s*[0-9a-fA-F]{2})*)')

    write_pcap_header(pcap_file)

    for line in input_file:
        m = frame_re.match(line)
        if m is None:
            continue
        if m.group(2):
            ts = float(m.group(2))
        else:
            ts = 0
        hexdata = m.group(3)
        hexdata = hexdata.split()
        data = ''.join([chr(int(x, 16)) for x in hexdata])
        pcap_addpacket(pcap_file, ts, data)

    input_file.close()
    pcap_file.close()
