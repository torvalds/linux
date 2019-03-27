#!/usr/bin/python
#
# Example Android logcat to wpa_supplicant wrapper for QR Code scans
# Copyright (c) 2017, Qualcomm Atheros, Inc.
#
# This software may be distributed under the terms of the BSD license.
# See README for more details.

import os
import sys
import argparse
import logging
import qrcode

scriptsdir = os.path.dirname(os.path.realpath(sys.modules[__name__].__file__))
sys.path.append(os.path.join(scriptsdir, '..', '..', 'wpaspy'))

import wpaspy

wpas_ctrl = '/var/run/wpa_supplicant'

def wpas_connect():
    ifaces = []
    if os.path.isdir(wpas_ctrl):
        try:
            ifaces = [os.path.join(wpas_ctrl, i) for i in os.listdir(wpas_ctrl)]
        except OSError, error:
            print "Could not find wpa_supplicant: ", error
            return None

    if len(ifaces) < 1:
        print "No wpa_supplicant control interface found"
        return None

    for ctrl in ifaces:
        try:
            wpas = wpaspy.Ctrl(ctrl)
            return wpas
        except Exception, e:
            pass
    return None

def dpp_logcat():
    for line in iter(sys.stdin.readline, ''):
        if "ResultHandler: Launching intent: Intent" not in line:
            continue
        if "act=android.intent.action.VIEW" not in line:
            continue
        uri = None
        for val in line.split(' '):
            if val.startswith('dat='):
                uri = val.split('=', 1)[1]
                break
        if not uri:
            continue
        if not uri.startswith('DPP:'):
            continue
        print "Found DPP bootstrap info URI:"
        print uri
        wpas = wpas_connect()
        if not wpas:
            print "Could not connect to wpa_supplicant"
            print
            continue
        res = wpas.request("DPP_QR_CODE " + uri);
        try:
            id = int(res)
        except ValueError:
            print "QR Code URI rejected"
            continue
        print "QR Code URI accepted - ID=%d" % id
        print wpas.request("DPP_BOOTSTRAP_INFO %d" % id)
        del wpas

def dpp_display(curve):
        wpas = wpas_connect()
        if not wpas:
            print "Could not connect to wpa_supplicant"
            return
        res = wpas.request("STATUS")
        addr = None
        for line in res.splitlines():
            if line.startswith("address="):
                addr = line.split('=')[1]
                break
        cmd = "DPP_BOOTSTRAP_GEN type=qrcode"
        cmd += " chan=81/1"
        if addr:
            cmd += " mac=" + addr.replace(':','')
        if curve:
            cmd += " curve=" + curve
        res = wpas.request(cmd)
        try:
            id = int(res)
        except ValueError:
            print "Failed to generate bootstrap info URI"
            return
        print "Bootstrap information - ID=%d" % id
        print wpas.request("DPP_BOOTSTRAP_INFO %d" % id)
        uri = wpas.request("DPP_BOOTSTRAP_GET_URI %d" % id)
        print uri
        print "ID=%d" % id
        qr = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_M,
                           border=3)
        qr.add_data(uri, optimize=5)
        qr.print_ascii(tty=True)
        print "ID=%d" % id
        del wpas

def main():
    parser = argparse.ArgumentParser(description='Android logcat to wpa_supplicant integration for DPP QR Code operations')
    parser.add_argument('-d', const=logging.DEBUG, default=logging.INFO,
                        action='store_const', dest='loglevel',
                        help='verbose debug output')
    parser.add_argument('--curve', '-c',
                        help='set a specific curve (P-256, P-384, P-521, BP-256R1, BP-384R1, BP-512R1) for key generation')
    parser.add_argument('command', choices=['logcat',
                                            'display'],
                        nargs='?')
    args = parser.parse_args()

    logging.basicConfig(level=args.loglevel)

    if args.command == "logcat":
        dpp_logcat()
    elif args.command == "display":
        dpp_display(args.curve)

if __name__ == '__main__':
    main()
