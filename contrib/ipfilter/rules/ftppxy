#!/bin/sh
# The proxy bit is as follows:
# proxy [port <portname>] <tag>/<protocol>
# the <tag> should match a tagname in the proxy table, as does the protocol.
# this format isn't finalised yet
echo "map ed0 0/0 -> 192.1.1.1/32 proxy port ftp ftp/tcp" | /sbin/ipnat -f -
