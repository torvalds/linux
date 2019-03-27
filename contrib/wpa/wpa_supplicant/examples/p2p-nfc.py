#!/usr/bin/python
#
# Example nfcpy to wpa_supplicant wrapper for P2P NFC operations
# Copyright (c) 2012-2013, Jouni Malinen <j@w1.fi>
#
# This software may be distributed under the terms of the BSD license.
# See README for more details.

import os
import sys
import time
import random
import threading
import argparse

import nfc
import nfc.ndef
import nfc.llcp
import nfc.handover

import logging

import wpaspy

wpas_ctrl = '/var/run/wpa_supplicant'
ifname = None
init_on_touch = False
in_raw_mode = False
prev_tcgetattr = 0
include_wps_req = True
include_p2p_req = True
no_input = False
srv = None
continue_loop = True
terminate_now = False
summary_file = None
success_file = None

def summary(txt):
    print txt
    if summary_file:
        with open(summary_file, 'a') as f:
            f.write(txt + "\n")

def success_report(txt):
    summary(txt)
    if success_file:
        with open(success_file, 'a') as f:
            f.write(txt + "\n")

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
        if ifname:
            if ifname not in ctrl:
                continue
        try:
            print "Trying to use control interface " + ctrl
            wpas = wpaspy.Ctrl(ctrl)
            return wpas
        except Exception, e:
            pass
    return None


def wpas_tag_read(message):
    wpas = wpas_connect()
    if (wpas == None):
        return False
    cmd = "WPS_NFC_TAG_READ " + str(message).encode("hex")
    global force_freq
    if force_freq:
        cmd = cmd + " freq=" + force_freq
    if "FAIL" in wpas.request(cmd):
        return False
    return True


def wpas_get_handover_req():
    wpas = wpas_connect()
    if (wpas == None):
        return None
    res = wpas.request("NFC_GET_HANDOVER_REQ NDEF P2P-CR").rstrip()
    if "FAIL" in res:
        return None
    return res.decode("hex")

def wpas_get_handover_req_wps():
    wpas = wpas_connect()
    if (wpas == None):
        return None
    res = wpas.request("NFC_GET_HANDOVER_REQ NDEF WPS-CR").rstrip()
    if "FAIL" in res:
        return None
    return res.decode("hex")


def wpas_get_handover_sel(tag=False):
    wpas = wpas_connect()
    if (wpas == None):
        return None
    if tag:
        res = wpas.request("NFC_GET_HANDOVER_SEL NDEF P2P-CR-TAG").rstrip()
    else:
	res = wpas.request("NFC_GET_HANDOVER_SEL NDEF P2P-CR").rstrip()
    if "FAIL" in res:
        return None
    return res.decode("hex")


def wpas_get_handover_sel_wps():
    wpas = wpas_connect()
    if (wpas == None):
        return None
    res = wpas.request("NFC_GET_HANDOVER_SEL NDEF WPS-CR");
    if "FAIL" in res:
        return None
    return res.rstrip().decode("hex")


def wpas_report_handover(req, sel, type):
    wpas = wpas_connect()
    if (wpas == None):
        return None
    cmd = "NFC_REPORT_HANDOVER " + type + " P2P " + str(req).encode("hex") + " " + str(sel).encode("hex")
    global force_freq
    if force_freq:
        cmd = cmd + " freq=" + force_freq
    return wpas.request(cmd)


def wpas_report_handover_wsc(req, sel, type):
    wpas = wpas_connect()
    if (wpas == None):
        return None
    cmd = "NFC_REPORT_HANDOVER " + type + " WPS " + str(req).encode("hex") + " " + str(sel).encode("hex")
    if force_freq:
        cmd = cmd + " freq=" + force_freq
    return wpas.request(cmd)


def p2p_handover_client(llc):
    message = nfc.ndef.HandoverRequestMessage(version="1.2")
    message.nonce = random.randint(0, 0xffff)

    global include_p2p_req
    if include_p2p_req:
        data = wpas_get_handover_req()
        if (data == None):
            summary("Could not get handover request carrier record from wpa_supplicant")
            return
        print "Handover request carrier record from wpa_supplicant: " + data.encode("hex")
        datamsg = nfc.ndef.Message(data)
        message.add_carrier(datamsg[0], "active", datamsg[1:])

    global include_wps_req
    if include_wps_req:
        print "Handover request (pre-WPS):"
        try:
            print message.pretty()
        except Exception, e:
            print e

        data = wpas_get_handover_req_wps()
        if data:
            print "Add WPS request in addition to P2P"
            datamsg = nfc.ndef.Message(data)
            message.add_carrier(datamsg[0], "active", datamsg[1:])

    print "Handover request:"
    try:
        print message.pretty()
    except Exception, e:
        print e
    print str(message).encode("hex")

    client = nfc.handover.HandoverClient(llc)
    try:
        summary("Trying to initiate NFC connection handover")
        client.connect()
        summary("Connected for handover")
    except nfc.llcp.ConnectRefused:
        summary("Handover connection refused")
        client.close()
        return
    except Exception, e:
        summary("Other exception: " + str(e))
        client.close()
        return

    summary("Sending handover request")

    if not client.send(message):
        summary("Failed to send handover request")
        client.close()
        return

    summary("Receiving handover response")
    message = client._recv()
    if message is None:
        summary("No response received")
        client.close()
        return
    if message.type != "urn:nfc:wkt:Hs":
        summary("Response was not Hs - received: " + message.type)
        client.close()
        return

    print "Received message"
    try:
        print message.pretty()
    except Exception, e:
        print e
    print str(message).encode("hex")
    message = nfc.ndef.HandoverSelectMessage(message)
    summary("Handover select received")
    try:
        print message.pretty()
    except Exception, e:
        print e

    for carrier in message.carriers:
        print "Remote carrier type: " + carrier.type
        if carrier.type == "application/vnd.wfa.p2p":
            print "P2P carrier type match - send to wpa_supplicant"
            if "OK" in wpas_report_handover(data, carrier.record, "INIT"):
                success_report("P2P handover reported successfully (initiator)")
            else:
                summary("P2P handover report rejected")
            break

    print "Remove peer"
    client.close()
    print "Done with handover"
    global only_one
    if only_one:
        print "only_one -> stop loop"
        global continue_loop
        continue_loop = False

    global no_wait
    if no_wait:
        print "Trying to exit.."
        global terminate_now
        terminate_now = True


class HandoverServer(nfc.handover.HandoverServer):
    def __init__(self, llc):
        super(HandoverServer, self).__init__(llc)
        self.sent_carrier = None
        self.ho_server_processing = False
        self.success = False

    # override to avoid parser error in request/response.pretty() in nfcpy
    # due to new WSC handover format
    def _process_request(self, request):
        summary("received handover request {}".format(request.type))
        response = nfc.ndef.Message("\xd1\x02\x01Hs\x12")
        if not request.type == 'urn:nfc:wkt:Hr':
            summary("not a handover request")
        else:
            try:
                request = nfc.ndef.HandoverRequestMessage(request)
            except nfc.ndef.DecodeError as e:
                summary("error decoding 'Hr' message: {}".format(e))
            else:
                response = self.process_request(request)
        summary("send handover response {}".format(response.type))
        return response

    def process_request(self, request):
        self.ho_server_processing = True
        clear_raw_mode()
        print "HandoverServer - request received"
        try:
            print "Parsed handover request: " + request.pretty()
        except Exception, e:
            print e

        sel = nfc.ndef.HandoverSelectMessage(version="1.2")

        found = False

        for carrier in request.carriers:
            print "Remote carrier type: " + carrier.type
            if carrier.type == "application/vnd.wfa.p2p":
                print "P2P carrier type match - add P2P carrier record"
                found = True
                self.received_carrier = carrier.record
                print "Carrier record:"
                try:
                    print carrier.record.pretty()
                except Exception, e:
                    print e
                data = wpas_get_handover_sel()
                if data is None:
                    print "Could not get handover select carrier record from wpa_supplicant"
                    continue
                print "Handover select carrier record from wpa_supplicant:"
                print data.encode("hex")
                self.sent_carrier = data
                if "OK" in wpas_report_handover(self.received_carrier, self.sent_carrier, "RESP"):
                    success_report("P2P handover reported successfully (responder)")
                else:
                    summary("P2P handover report rejected")
                    break

                message = nfc.ndef.Message(data);
                sel.add_carrier(message[0], "active", message[1:])
                break

        for carrier in request.carriers:
            if found:
                break
            print "Remote carrier type: " + carrier.type
            if carrier.type == "application/vnd.wfa.wsc":
                print "WSC carrier type match - add WSC carrier record"
                found = True
                self.received_carrier = carrier.record
                print "Carrier record:"
                try:
                    print carrier.record.pretty()
                except Exception, e:
                    print e
                data = wpas_get_handover_sel_wps()
                if data is None:
                    print "Could not get handover select carrier record from wpa_supplicant"
                    continue
                print "Handover select carrier record from wpa_supplicant:"
                print data.encode("hex")
                self.sent_carrier = data
                if "OK" in wpas_report_handover_wsc(self.received_carrier, self.sent_carrier, "RESP"):
                    success_report("WSC handover reported successfully")
                else:
                    summary("WSC handover report rejected")
                    break

                message = nfc.ndef.Message(data);
                sel.add_carrier(message[0], "active", message[1:])
                found = True
                break

        print "Handover select:"
        try:
            print sel.pretty()
        except Exception, e:
            print e
        print str(sel).encode("hex")

        summary("Sending handover select")
        self.success = True
        return sel


def clear_raw_mode():
    import sys, tty, termios
    global prev_tcgetattr, in_raw_mode
    if not in_raw_mode:
        return
    fd = sys.stdin.fileno()
    termios.tcsetattr(fd, termios.TCSADRAIN, prev_tcgetattr)
    in_raw_mode = False


def getch():
    import sys, tty, termios, select
    global prev_tcgetattr, in_raw_mode
    fd = sys.stdin.fileno()
    prev_tcgetattr = termios.tcgetattr(fd)
    ch = None
    try:
        tty.setraw(fd)
        in_raw_mode = True
        [i, o, e] = select.select([fd], [], [], 0.05)
        if i:
            ch = sys.stdin.read(1)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, prev_tcgetattr)
        in_raw_mode = False
    return ch


def p2p_tag_read(tag):
    success = False
    if len(tag.ndef.message):
        for record in tag.ndef.message:
            print "record type " + record.type
            if record.type == "application/vnd.wfa.wsc":
                summary("WPS tag - send to wpa_supplicant")
                success = wpas_tag_read(tag.ndef.message)
                break
            if record.type == "application/vnd.wfa.p2p":
                summary("P2P tag - send to wpa_supplicant")
                success = wpas_tag_read(tag.ndef.message)
                break
    else:
        summary("Empty tag")

    if success:
        success_report("Tag read succeeded")

    return success


def rdwr_connected_p2p_write(tag):
    summary("Tag found - writing - " + str(tag))
    global p2p_sel_data
    tag.ndef.message = str(p2p_sel_data)
    success_report("Tag write succeeded")
    print "Done - remove tag"
    global only_one
    if only_one:
        global continue_loop
        continue_loop = False
    global p2p_sel_wait_remove
    return p2p_sel_wait_remove

def wps_write_p2p_handover_sel(clf, wait_remove=True):
    print "Write P2P handover select"
    data = wpas_get_handover_sel(tag=True)
    if (data == None):
        summary("Could not get P2P handover select from wpa_supplicant")
        return

    global p2p_sel_wait_remove
    p2p_sel_wait_remove = wait_remove
    global p2p_sel_data
    p2p_sel_data = nfc.ndef.HandoverSelectMessage(version="1.2")
    message = nfc.ndef.Message(data);
    p2p_sel_data.add_carrier(message[0], "active", message[1:])
    print "Handover select:"
    try:
        print p2p_sel_data.pretty()
    except Exception, e:
        print e
    print str(p2p_sel_data).encode("hex")

    print "Touch an NFC tag"
    clf.connect(rdwr={'on-connect': rdwr_connected_p2p_write})


def rdwr_connected(tag):
    global only_one, no_wait
    summary("Tag connected: " + str(tag))

    if tag.ndef:
        print "NDEF tag: " + tag.type
        try:
            print tag.ndef.message.pretty()
        except Exception, e:
            print e
        success = p2p_tag_read(tag)
        if only_one and success:
            global continue_loop
            continue_loop = False
    else:
        summary("Not an NDEF tag - remove tag")
        return True

    return not no_wait


def llcp_worker(llc):
    global init_on_touch
    if init_on_touch:
            print "Starting handover client"
            p2p_handover_client(llc)
            return

    global no_input
    if no_input:
        print "Wait for handover to complete"
    else:
        print "Wait for handover to complete - press 'i' to initiate ('w' for WPS only, 'p' for P2P only)"
    global srv
    global wait_connection
    while not wait_connection and srv.sent_carrier is None:
        if srv.ho_server_processing:
            time.sleep(0.025)
        elif no_input:
            time.sleep(0.5)
        else:
            global include_wps_req, include_p2p_req
            res = getch()
            if res == 'i':
                include_wps_req = True
                include_p2p_req = True
            elif res == 'p':
                include_wps_req = False
                include_p2p_req = True
            elif res == 'w':
                include_wps_req = True
                include_p2p_req = False
            else:
                continue
            clear_raw_mode()
            print "Starting handover client"
            p2p_handover_client(llc)
            return
            
    clear_raw_mode()
    print "Exiting llcp_worker thread"

def llcp_startup(clf, llc):
    print "Start LLCP server"
    global srv
    srv = HandoverServer(llc)
    return llc

def llcp_connected(llc):
    print "P2P LLCP connected"
    global wait_connection
    wait_connection = False
    global init_on_touch
    if not init_on_touch:
        global srv
        srv.start()
    if init_on_touch or not no_input:
        threading.Thread(target=llcp_worker, args=(llc,)).start()
    return True

def terminate_loop():
    global terminate_now
    return terminate_now

def main():
    clf = nfc.ContactlessFrontend()

    parser = argparse.ArgumentParser(description='nfcpy to wpa_supplicant integration for P2P and WPS NFC operations')
    parser.add_argument('-d', const=logging.DEBUG, default=logging.INFO,
                        action='store_const', dest='loglevel',
                        help='verbose debug output')
    parser.add_argument('-q', const=logging.WARNING, action='store_const',
                        dest='loglevel', help='be quiet')
    parser.add_argument('--only-one', '-1', action='store_true',
                        help='run only one operation and exit')
    parser.add_argument('--init-on-touch', '-I', action='store_true',
                        help='initiate handover on touch')
    parser.add_argument('--no-wait', action='store_true',
                        help='do not wait for tag to be removed before exiting')
    parser.add_argument('--ifname', '-i',
                        help='network interface name')
    parser.add_argument('--no-wps-req', '-N', action='store_true',
                        help='do not include WPS carrier record in request')
    parser.add_argument('--no-input', '-a', action='store_true',
                        help='do not use stdout input to initiate handover')
    parser.add_argument('--tag-read-only', '-t', action='store_true',
                        help='tag read only (do not allow connection handover)')
    parser.add_argument('--handover-only', action='store_true',
                        help='connection handover only (do not allow tag read)')
    parser.add_argument('--freq', '-f',
                        help='forced frequency of operating channel in MHz')
    parser.add_argument('--summary',
                        help='summary file for writing status updates')
    parser.add_argument('--success',
                        help='success file for writing success update')
    parser.add_argument('command', choices=['write-p2p-sel'],
                        nargs='?')
    args = parser.parse_args()

    global only_one
    only_one = args.only_one

    global no_wait
    no_wait = args.no_wait

    global force_freq
    force_freq = args.freq

    logging.basicConfig(level=args.loglevel)

    global init_on_touch
    init_on_touch = args.init_on_touch

    if args.ifname:
        global ifname
        ifname = args.ifname
        print "Selected ifname " + ifname

    if args.no_wps_req:
        global include_wps_req
        include_wps_req = False

    if args.summary:
        global summary_file
        summary_file = args.summary

    if args.success:
        global success_file
        success_file = args.success

    if args.no_input:
        global no_input
        no_input = True

    clf = nfc.ContactlessFrontend()
    global wait_connection

    try:
        if not clf.open("usb"):
            print "Could not open connection with an NFC device"
            raise SystemExit

        if args.command == "write-p2p-sel":
            wps_write_p2p_handover_sel(clf, wait_remove=not args.no_wait)
            raise SystemExit

        global continue_loop
        while continue_loop:
            print "Waiting for a tag or peer to be touched"
            wait_connection = True
            try:
                if args.tag_read_only:
                    if not clf.connect(rdwr={'on-connect': rdwr_connected}):
                        break
                elif args.handover_only:
                    if not clf.connect(llcp={'on-startup': llcp_startup,
                                             'on-connect': llcp_connected},
                                       terminate=terminate_loop):
                        break
                else:
                    if not clf.connect(rdwr={'on-connect': rdwr_connected},
                                       llcp={'on-startup': llcp_startup,
                                             'on-connect': llcp_connected},
                                       terminate=terminate_loop):
                        break
            except Exception, e:
                print "clf.connect failed"

            global srv
            if only_one and srv and srv.success:
                raise SystemExit

    except KeyboardInterrupt:
        raise SystemExit
    finally:
        clf.close()

    raise SystemExit

if __name__ == '__main__':
    main()
