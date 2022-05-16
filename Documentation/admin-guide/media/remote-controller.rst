.. SPDX-License-Identifier: GPL-2.0

======================================================
Infrared remote control support in video4linux drivers
======================================================

Authors: Gerd Hoffmann, Mauro Carvalho Chehab

Basics
======

Most analog and digital TV boards support remote controllers. Several of
them have a microprocessor that receives the IR carriers, convert into
pulse/space sequences and then to scan codes, returning such codes to
userspace ("scancode mode"). Other boards return just the pulse/space
sequences ("raw mode").

The support for remote controller in scancode mode is provided by the
standard Linux input layer. The support for raw mode is provided via LIRC.

In order to check the support and test it, it is suggested to download
the `v4l-utils <https://git.linuxtv.org/v4l-utils.git/>`_. It provides
two tools to handle remote controllers:

- ir-keytable: provides a way to query the remote controller, list the
  protocols it supports, enable in-kernel support for IR decoder or
  switch the protocol and to test the reception of scan codes;

- ir-ctl: provide tools to handle remote controllers that support raw mode
  via LIRC interface.

Usually, the remote controller module is auto-loaded when the TV card is
detected. However, for a few devices, you need to manually load the
ir-kbd-i2c module.

How it works
============

The modules register the remote as keyboard within the linux input
layer, i.e. you'll see the keys of the remote as normal key strokes
(if CONFIG_INPUT_KEYBOARD is enabled).

Using the event devices (CONFIG_INPUT_EVDEV) it is possible for
applications to access the remote via /dev/input/event<n> devices.
The udev/systemd will automatically create the devices. If you install
the `v4l-utils <https://git.linuxtv.org/v4l-utils.git/>`_, it may also
automatically load a different keytable than the default one. Please see
`v4l-utils <https://git.linuxtv.org/v4l-utils.git/>`_ ir-keytable.1
man page for details.

The ir-keytable tool is nice for trouble shooting, i.e. to check
whenever the input device is really present, which of the devices it
is, check whenever pressing keys on the remote actually generates
events and the like.  You can also use any other input utility that changes
the keymaps, like the input kbd utility.


Using with lircd
----------------

The latest versions of the lircd daemon supports reading events from the
linux input layer (via event device). It also supports receiving IR codes
in lirc mode.


Using without lircd
-------------------

Xorg recognizes several IR keycodes that have its numerical value lower
than 247. With the advent of Wayland, the input driver got updated too,
and should now accept all keycodes. Yet, you may want to just reasign
the keycodes to something that your favorite media application likes.

This can be done by setting
`v4l-utils <https://git.linuxtv.org/v4l-utils.git/>`_ to load your own
keytable in runtime. Please read  ir-keytable.1 man page for details.
