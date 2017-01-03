==============================================================
Alsa driver for Digigram miXart8 and miXart8AES/EBU soundcards
==============================================================

Digigram <alsa@digigram.com>


GENERAL
=======

The miXart8 is a multichannel audio processing and mixing soundcard
that has 4 stereo audio inputs and 4 stereo audio outputs.
The miXart8AES/EBU is the same with a add-on card that offers further
4 digital stereo audio inputs and outputs.
Furthermore the add-on card offers external clock synchronisation
(AES/EBU, Word Clock, Time Code and Video Synchro)

The mainboard has a PowerPC that offers onboard mpeg encoding and
decoding, samplerate conversions and various effects.

The driver don't work properly at all until the certain firmwares
are loaded, i.e. no PCM nor mixer devices will appear.
Use the mixartloader that can be found in the alsa-tools package.


VERSION 0.1.0
=============

One miXart8 board will be represented as 4 alsa cards, each with 1
stereo analog capture 'pcm0c' and 1 stereo analog playback 'pcm0p' device.
With a miXart8AES/EBU there is in addition 1 stereo digital input
'pcm1c' and 1 stereo digital output 'pcm1p' per card.

Formats
-------
U8, S16_LE, S16_BE, S24_3LE, S24_3BE, FLOAT_LE, FLOAT_BE
Sample rates : 8000 - 48000 Hz continuously

Playback
--------
For instance the playback devices are configured to have max. 4
substreams performing hardware mixing. This could be changed to a
maximum of 24 substreams if wished.
Mono files will be played on the left and right channel. Each channel
can be muted for each stream to use 8 analog/digital outputs separately.

Capture
-------
There is one substream per capture device. For instance only stereo
formats are supported.

Mixer
-----
<Master> and <Master Capture>
	analog volume control of playback and capture PCM.
<PCM 0-3> and <PCM Capture>
	digital volume control of each analog substream.
<AES 0-3> and <AES Capture>
	digital volume control of each AES/EBU substream.
<Monitoring>
	Loopback from 'pcm0c' to 'pcm0p' with digital volume
	and mute control.

Rem : for best audio quality try to keep a 0 attenuation on the PCM
and AES volume controls which is set by 219 in the range from 0 to 255
(about 86% with alsamixer)


NOT YET IMPLEMENTED
===================

- external clock support (AES/EBU, Word Clock, Time Code, Video Sync)
- MPEG audio formats
- mono record
- on-board effects and samplerate conversions
- linked streams


FIRMWARE
========

[As of 2.6.11, the firmware can be loaded automatically with hotplug
 when CONFIG_FW_LOADER is set.  The mixartloader is necessary only
 for older versions or when you build the driver into kernel.]
 
For loading the firmware automatically after the module is loaded, use a
install command.  For example, add the following entry to
/etc/modprobe.d/mixart.conf for miXart driver:
::

	install snd-mixart /sbin/modprobe --first-time -i snd-mixart && \
			   /usr/bin/mixartloader


(for 2.2/2.4 kernels, add "post-install snd-mixart /usr/bin/vxloader" to
/etc/modules.conf, instead.)

The firmware binaries are installed on /usr/share/alsa/firmware
(or /usr/local/share/alsa/firmware, depending to the prefix option of
configure).  There will be a miXart.conf file, which define the dsp image
files.

The firmware files are copyright by Digigram SA


COPYRIGHT
=========

Copyright (c) 2003 Digigram SA <alsa@digigram.com>
Distributable under GPL.
