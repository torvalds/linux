.. -*- coding: utf-8; mode: rst -*-

.. _lirc_dev_intro:

************
Introduction
************

The LIRC device interface is a bi-directional interface for transporting
raw IR data between userspace and kernelspace. Fundamentally, it is just
a chardev (/dev/lircX, for X = 0, 1, 2, ...), with a number of standard
struct file_operations defined on it. With respect to transporting raw
IR data to and fro, the essential fops are read, write and ioctl.

Example dmesg output upon a driver registering w/LIRC:

.. code-block:: none

    $ dmesg |grep lirc_dev
    lirc_dev: IR Remote Control driver registered, major 248
    rc rc0: lirc_dev: driver ir-lirc-codec (mceusb) registered at minor = 0

What you should see for a chardev:

.. code-block:: none

    $ ls -l /dev/lirc*
    crw-rw---- 1 root root 248, 0 Jul 2 22:20 /dev/lirc0
