
infrared remote control support in video4linux drivers
======================================================


basics
------

Current versions use the linux input layer to support infrared
remote controls.  I suggest to download my input layer tools
from http://bytesex.org/snapshot/input-<date>.tar.gz

Modules you have to load:

  saa7134	statically built in, i.e. just the driver :)
  bttv		ir-kbd-gpio or ir-kbd-i2c depending on your
		card.

ir-kbd-gpio and ir-kbd-i2c don't support all cards lirc supports
(yet), mainly for the reason that the code of lirc_i2c and lirc_gpio
was very confusing and I decided to basically start over from scratch.
Feel free to contact me in case of trouble.  Note that the ir-kbd-*
modules work on 2.6.x kernels only through ...


how it works
------------

The modules register the remote as keyboard within the linux input
layer, i.e. you'll see the keys of the remote as normal key strokes
(if CONFIG_INPUT_KEYBOARD is enabled).

Using the event devices (CONFIG_INPUT_EVDEV) it is possible for
applications to access the remote via /dev/input/event<n> devices.
You might have to create the special files using "/sbin/MAKEDEV
input".  The input layer tools mentioned above use the event device.

The input layer tools are nice for trouble shooting, i.e. to check
whenever the input device is really present, which of the devices it
is, check whenever pressing keys on the remote actually generates
events and the like.  You can also use the kbd utility to change the
keymaps (2.6.x kernels only through).


using with lircd
================

The cvs version of the lircd daemon supports reading events from the
linux input layer (via event device).  The input layer tools tarball
comes with a lircd config file.


using without lircd
===================

XFree86 likely can be configured to recognise the remote keys.  Once I
simply tried to configure one of the multimedia keyboards as input
device, which had the effect that XFree86 recognised some of the keys
of my remote control and passed volume up/down key presses as
XF86AudioRaiseVolume and XF86AudioLowerVolume key events to the X11
clients.

It likely is possible to make that fly with a nice xkb config file,
I know next to nothing about that through.


Have fun,

  Gerd

--
Gerd Knorr <kraxel@bytesex.org>
