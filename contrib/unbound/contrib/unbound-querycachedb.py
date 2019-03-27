#!/usr/bin/env python

import hashlib
import sys
import struct
import socket
import time
from optparse import OptionParser

import dns.message
import dns.name
import dns.rdataclass
import dns.rdatatype

def _calc_hashkey(qname, secret, qtype):
    qclass = 'IN'           # CLASS is fixed for simplicity
    hobj = hashlib.sha256()
    hobj.update(dns.name.from_text(qname).to_wire())
    hobj.update(struct.pack('HH',
                            socket.htons(dns.rdatatype.from_text(qtype)),
                            socket.htons(dns.rdataclass.from_text(qclass))))
    hobj.update(secret)
    return hobj.hexdigest().upper()

def _redis_get(options, key):
    import redis
    return redis.Redis(options.address, int(options.port)).get(key)

def _dump_value(options, qname, key, value):
    print(';; query=%s/IN/%s' % (qname, options.qtype))
    print(';; key=%s' % key)
    if value is None:
        print(';; no value')
        return
    if len(value) < 16:
        print(';; broken value, short length: %d' % len(value))
        return
    now = int(time.time())
    timestamp = struct.unpack('!Q', value[-16:-8])[0]
    expire = struct.unpack('!Q', value[-8:])[0]
    print(';; Now=%d, TimeStamp=%d, Expire=%d, TTL=%d' %
          (now, timestamp, expire, max(expire - now, 0)))
    print(dns.message.from_wire(value[:-16]))

def main():
    parser = OptionParser(usage='usage: %prog [options] query_name')
    parser.add_option("-a", "--address", dest="address", action="store",
                      default='127.0.0.1', help="backend-server address",
                      metavar='ADDRESS')
    parser.add_option("-b", "--backend", dest="backend", action="store",
                      default='redis', help="backend name",
                      metavar='BACKEND')
    parser.add_option("-p", "--port", dest="port", action="store",
                      default='6379', help="backend-server port",
                      metavar='PORT')
    parser.add_option("-s", "--secret", dest="secret", action="store",
                      default='default', help="secret seed", metavar='SECRET')
    parser.add_option("-t", "--qtype", dest="qtype", action="store",
                      default='A', help="query RR type", metavar='QTYPE')

    (options, args) = parser.parse_args()
    if len(args) < 1:
        parser.error('qname is missing')
    if options.backend == 'redis':
        get_func = _redis_get
    else:
        raise Exception('unknown backend name: %s\n' % options.backend)
    key = _calc_hashkey(args[0], options.secret, options.qtype)
    value = get_func(options, key)
    _dump_value(options, args[0], key, value)

if __name__ == '__main__':
    try:
        main()
    except Exception as e:
        sys.stderr.write('%s\n' % e)
        exit(1)
