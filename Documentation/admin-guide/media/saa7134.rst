.. SPDX-License-Identifier: GPL-2.0

The saa7134 driver
==================

Author Gerd Hoffmann


This is a v4l2/oss device driver for saa7130/33/34/35 based capture / TV
boards.


Status
------

Almost everything is working.  video, sound, tuner, radio, mpeg ts, ...

As with bttv, card-specific tweaks are needed.  Check CARDLIST for a
list of known TV cards and saa7134-cards.c for the drivers card
configuration info.


Build
-----

Once you pick up a Kernel source, you should configure, build,
install and boot the new kernel.  You'll need at least
these config options::

    ./scripts/config -e PCI
    ./scripts/config -e INPUT
    ./scripts/config -m I2C
    ./scripts/config -m MEDIA_SUPPORT
    ./scripts/config -e MEDIA_PCI_SUPPORT
    ./scripts/config -e MEDIA_ANALOG_TV_SUPPORT
    ./scripts/config -e MEDIA_DIGITAL_TV_SUPPORT
    ./scripts/config -e MEDIA_RADIO_SUPPORT
    ./scripts/config -e RC_CORE
    ./scripts/config -e MEDIA_SUBDRV_AUTOSELECT
    ./scripts/config -m VIDEO_SAA7134
    ./scripts/config -e SAA7134_ALSA
    ./scripts/config -e VIDEO_SAA7134_RC
    ./scripts/config -e VIDEO_SAA7134_DVB
    ./scripts/config -e VIDEO_SAA7134_GO7007

To build and install, you should run::

    make && make modules_install && make install

Once the new Kernel is booted, saa7134 driver should be loaded automatically.

Depending on the card you might have to pass ``card=<nr>`` as insmod option.
If so, please check Documentation/admin-guide/media/saa7134-cardlist.rst
for valid choices.

Once you have your card type number, you can pass a modules configuration
via a file (usually, it is either ``/etc/modules.conf`` or some file at
``/etc/modules-load.d/``, but the actual place depends on your
distribution), with this content::

    options saa7134 card=13 # Assuming that your card type is #13


Changes / Fixes
---------------

Please mail to linux-media AT vger.kernel.org unified diffs against
the linux media git tree:

    https://git.linuxtv.org/media_tree.git/

This is done by committing a patch at a clone of the git tree and
submitting the patch using ``git send-email``. Don't forget to
describe at the lots  what it changes / which problem it fixes / whatever
it is good for ...


Known Problems
--------------

* The tuner for the flyvideos isn't detected automatically and the
  default might not work for you depending on which version you have.
  There is a ``tuner=`` insmod option to override the driver's default.

Credits
-------

andrew.stevens@philips.com + werner.leeb@philips.com for providing
saa7134 hardware specs and sample board.
