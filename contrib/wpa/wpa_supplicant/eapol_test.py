#!/usr/bin/env python2
#
# eapol_test controller
# Copyright (c) 2015, Jouni Malinen <j@w1.fi>
#
# This software may be distributed under the terms of the BSD license.
# See README for more details.

import argparse
import logging
import os
import Queue
import sys
import threading

logger = logging.getLogger()
dir = os.path.dirname(os.path.realpath(sys.modules[__name__].__file__))
sys.path.append(os.path.join(dir, '..', 'wpaspy'))
import wpaspy
wpas_ctrl = '/tmp/eapol_test'

class eapol_test:
    def __init__(self, ifname):
        self.ifname = ifname
        self.ctrl = wpaspy.Ctrl(os.path.join(wpas_ctrl, ifname))
        if "PONG" not in self.ctrl.request("PING"):
            raise Exception("Failed to connect to eapol_test (%s)" % ifname)
        self.mon = wpaspy.Ctrl(os.path.join(wpas_ctrl, ifname))
        self.mon.attach()

    def add_network(self):
        id = self.request("ADD_NETWORK")
        if "FAIL" in id:
            raise Exception("ADD_NETWORK failed")
        return int(id)

    def remove_network(self, id):
        id = self.request("REMOVE_NETWORK " + str(id))
        if "FAIL" in id:
            raise Exception("REMOVE_NETWORK failed")
        return None

    def set_network(self, id, field, value):
        res = self.request("SET_NETWORK " + str(id) + " " + field + " " + value)
        if "FAIL" in res:
            raise Exception("SET_NETWORK failed")
        return None

    def set_network_quoted(self, id, field, value):
        res = self.request("SET_NETWORK " + str(id) + " " + field + ' "' + value + '"')
        if "FAIL" in res:
            raise Exception("SET_NETWORK failed")
        return None

    def request(self, cmd, timeout=10):
        return self.ctrl.request(cmd, timeout=timeout)

    def wait_event(self, events, timeout=10):
        start = os.times()[4]
        while True:
            while self.mon.pending():
                ev = self.mon.recv()
                logger.debug(self.ifname + ": " + ev)
                for event in events:
                    if event in ev:
                        return ev
            now = os.times()[4]
            remaining = start + timeout - now
            if remaining <= 0:
                break
            if not self.mon.pending(timeout=remaining):
                break
        return None

def run(ifname, count, no_fast_reauth, res):
    et = eapol_test(ifname)

    et.request("AP_SCAN 0")
    if no_fast_reauth:
        et.request("SET fast_reauth 0")
    else:
        et.request("SET fast_reauth 1")
    id = et.add_network()
    et.set_network(id, "key_mgmt", "IEEE8021X")
    et.set_network(id, "eapol_flags", "0")
    et.set_network(id, "eap", "TLS")
    et.set_network_quoted(id, "identity", "user")
    et.set_network_quoted(id, "ca_cert", 'ca.pem')
    et.set_network_quoted(id, "client_cert", 'client.pem')
    et.set_network_quoted(id, "private_key", 'client.key')
    et.set_network_quoted(id, "private_key_passwd", 'whatever')
    et.set_network(id, "disabled", "0")

    fail = False
    for i in range(count):
        et.request("REASSOCIATE")
        ev = et.wait_event(["CTRL-EVENT-CONNECTED", "CTRL-EVENT-EAP-FAILURE"])
        if ev is None or "CTRL-EVENT-CONNECTED" not in ev:
            fail = True
            break

    et.remove_network(id)

    if fail:
        res.put("FAIL (%d OK)" % i)
    else:
        res.put("PASS %d" % (i + 1))

def main():
    parser = argparse.ArgumentParser(description='eapol_test controller')
    parser.add_argument('--ctrl', help='control interface directory')
    parser.add_argument('--num', help='number of processes')
    parser.add_argument('--iter', help='number of iterations')
    parser.add_argument('--no-fast-reauth', action='store_true',
                        dest='no_fast_reauth',
                        help='disable TLS session resumption')
    args = parser.parse_args()

    num = int(args.num)
    iter = int(args.iter)
    if args.ctrl:
        global wpas_ctrl
        wpas_ctrl = args.ctrl

    t = {}
    res = {}
    for i in range(num):
        res[i] = Queue.Queue()
        t[i] = threading.Thread(target=run, args=(str(i), iter,
                                                  args.no_fast_reauth, res[i]))
    for i in range(num):
        t[i].start()
    for i in range(num):
        t[i].join()
        try:
            results = res[i].get(False)
        except:
            results = "N/A"
        print "%d: %s" % (i, results)

if __name__ == "__main__":
    main()
