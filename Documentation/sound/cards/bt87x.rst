=================
ALSA BT87x Driver
=================

Intro
=====

You might have noticed that the bt878 grabber cards have actually
*two* PCI functions:
::

  $ lspci
  [ ... ]
  00:0a.0 Multimedia video controller: Brooktree Corporation Bt878 (rev 02)
  00:0a.1 Multimedia controller: Brooktree Corporation Bt878 (rev 02)
  [ ... ]

The first does video, it is backward compatible to the bt848.  The second
does audio.  snd-bt87x is a driver for the second function.  It's a sound
driver which can be used for recording sound (and *only* recording, no
playback).  As most TV cards come with a short cable which can be plugged
into your sound card's line-in you probably don't need this driver if all
you want to do is just watching TV...

Some cards do not bother to connect anything to the audio input pins of
the chip, and some other cards use the audio function to transport MPEG
video data, so it's quite possible that audio recording may not work
with your card.


Driver Status
=============

The driver is now stable.  However, it doesn't know about many TV cards,
and it refuses to load for cards it doesn't know.

If the driver complains ("Unknown TV card found, the audio driver will
not load"), you can specify the ``load_all=1`` option to force the driver to
try to use the audio capture function of your card.  If the frequency of
recorded data is not right, try to specify the ``digital_rate`` option with
other values than the default 32000 (often it's 44100 or 64000).

If you have an unknown card, please mail the ID and board name to
<alsa-devel@alsa-project.org>, regardless of whether audio capture works
or not, so that future versions of this driver know about your card.


Audio modes
===========

The chip knows two different modes (digital/analog).  snd-bt87x
registers two PCM devices, one for each mode.  They cannot be used at
the same time.


Digital audio mode
==================

The first device (hw:X,0) gives you 16 bit stereo sound.  The sample
rate depends on the external source which feeds the Bt87x with digital
sound via I2S interface.


Analog audio mode (A/D)
=======================

The second device (hw:X,1) gives you 8 or 16 bit mono sound.  Supported
sample rates are between 119466 and 448000 Hz (yes, these numbers are
that high).  If you've set the CONFIG_SND_BT87X_OVERCLOCK option, the
maximum sample rate is 1792000 Hz, but audio data becomes unusable
beyond 896000 Hz on my card.

The chip has three analog inputs.  Consequently you'll get a mixer
device to control these.


Have fun,

  Clemens


Written by Clemens Ladisch <clemens@ladisch.de>
big parts copied from btaudio.txt by Gerd Knorr <kraxel@bytesex.org>
