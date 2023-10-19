.. SPDX-License-Identifier: GPL-2.0

=====================
IOAM6 Sysfs variables
=====================


/proc/sys/net/conf/<iface>/ioam6_* variables:
=============================================

ioam6_enabled - BOOL
        Accept (= enabled) or ignore (= disabled) IPv6 IOAM options on ingress
        for this interface.

        * 0 - disabled (default)
        * 1 - enabled

ioam6_id - SHORT INTEGER
        Define the IOAM id of this interface.

        Default is ~0.

ioam6_id_wide - INTEGER
        Define the wide IOAM id of this interface.

        Default is ~0.
