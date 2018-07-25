#/bin/bash
dmesg -C
rm -fr /var/log/*
service rsyslog restart
