.. SPDX-License-Identifier: GPL-2.0

========================================
Manual parsing of HID report descriptors
========================================

Consider again the mouse HID report descriptor
introduced in Documentation/hid/hidintro.rst::

  $ hexdump -C /sys/bus/hid/devices/0003\:093A\:2510.0002/report_descriptor
  00000000  05 01 09 02 a1 01 09 01  a1 00 05 09 19 01 29 03  |..............).|
  00000010  15 00 25 01 75 01 95 03  81 02 75 05 95 01 81 01  |..%.u.....u.....|
  00000020  05 01 09 30 09 31 09 38  15 81 25 7f 75 08 95 03  |...0.1.8..%.u...|
  00000030  81 06 c0 c0                                       |....|
  00000034

and try to parse it by hand.

Start with the first number, 0x05: it carries 2 bits for the
length of the item, 2 bits for the type of the item and 4 bits for the
function::

  +----------+
  | 00000101 |
  +----------+
          ^^
          ---- Length of data (see HID spec 6.2.2.2)
        ^^
        ------ Type of the item (see HID spec 6.2.2.2, then jump to 6.2.2.7)
    ^^^^
    --------- Function of the item (see HID spec 6.2.2.7, then HUT Sec 3)

In our case, the length is 1 byte, the type is ``Global`` and the
function is ``Usage Page``, thus for parsing the value 0x01 in the second byte
we need to refer to HUT Sec 3.

The second number is the actual data, and its meaning can be found in
the HUT. We have a ``Usage Page``, thus we need to refer to HUT
Sec. 3, "Usage Pages"; from there, one sees that ``0x01`` stands for
``Generic Desktop Page``.

Moving now to the second two bytes, and following the same scheme,
``0x09`` (i.e. ``00001001``) will be followed by one byte (``01``)
and is a ``Local`` item (``10``). Thus, the meaning of the remaining four bits
(``0000``) is given in the HID spec Sec. 6.2.2.8 "Local Items", so that
we have a ``Usage``. From HUT, Sec. 4, "Generic Desktop Page",  we see that
0x02 stands for ``Mouse``.

The following numbers can be parsed in the same way.
