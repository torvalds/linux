===========================
Standard ALSA Control Names
===========================

This document describes standard names of mixer controls.

Standard Syntax
---------------
Syntax: [LOCATION] SOURCE [CHANNEL] [DIRECTION] FUNCTION


DIRECTION
~~~~~~~~~
================	===============
<nothing>		both directions
Playback		one direction
Capture			one direction
Bypass Playback		one direction
Bypass Capture		one direction
================	===============

FUNCTION
~~~~~~~~
========	=================================
Switch		on/off switch
Volume		amplifier
Route		route control, hardware specific
========	=================================

CHANNEL
~~~~~~~
============	==================================================
<nothing>	channel independent, or applies to all channels
Front		front left/right channels
Surround	rear left/right in 4.0/5.1 surround
CLFE		C/LFE channels
Center		center cannel
LFE		LFE channel
Side		side left/right for 7.1 surround
============	==================================================

LOCATION (Physical location of source)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
============	=====================
Front		front position
Rear		rear position
Dock		on docking station
Internal	internal
============	=====================

SOURCE
~~~~~~
===================	=================================================
Master
Master Mono
Hardware Master
Speaker			internal speaker
Bass Speaker		internal LFE speaker
Headphone
Line Out
Beep			beep generator
Phone
Phone Input
Phone Output
Synth
FM
Mic
Headset Mic		mic part of combined headset jack - 4-pin
			headphone + mic
Headphone Mic		mic part of either/or - 3-pin headphone or mic
Line			input only, use "Line Out" for output
CD
Video
Zoom Video
Aux
PCM
PCM Pan
Loopback
Analog Loopback		D/A -> A/D loopback
Digital Loopback	playback -> capture loopback -
			without analog path
Mono
Mono Output
Multi
ADC
Wave
Music
I2S
IEC958
HDMI
SPDIF			output only
SPDIF In
Digital In
HDMI/DP			either HDMI or DisplayPort
===================	=================================================

Exceptions (deprecated)
-----------------------

=====================================	=======================
[Analogue|Digital] Capture Source
[Analogue|Digital] Capture Switch	aka input gain switch
[Analogue|Digital] Capture Volume	aka input gain volume
[Analogue|Digital] Playback Switch	aka output gain switch
[Analogue|Digital] Playback Volume	aka output gain volume
Tone Control - Switch
Tone Control - Bass
Tone Control - Treble
3D Control - Switch
3D Control - Center
3D Control - Depth
3D Control - Wide
3D Control - Space
3D Control - Level
Mic Boost [(?dB)]
=====================================	=======================

PCM interface
-------------

===================	========================================
Sample Clock Source	{ "Word", "Internal", "AutoSync" }
Clock Sync Status	{ "Lock", "Sync", "No Lock" }
External Rate		external capture rate
Capture Rate		capture rate taken from external source
===================	========================================

IEC958 (S/PDIF) interface
-------------------------

============================================	======================================
IEC958 [...] [Playback|Capture] Switch		turn on/off the IEC958 interface
IEC958 [...] [Playback|Capture] Volume		digital volume control
IEC958 [...] [Playback|Capture] Default		default or global value - read/write
IEC958 [...] [Playback|Capture] Mask		consumer and professional mask
IEC958 [...] [Playback|Capture] Con Mask	consumer mask
IEC958 [...] [Playback|Capture] Pro Mask	professional mask
IEC958 [...] [Playback|Capture] PCM Stream	the settings assigned to a PCM stream
IEC958 Q-subcode [Playback|Capture] Default	Q-subcode bits

IEC958 Preamble [Playback|Capture] Default	burst preamble words (4*16bits)
============================================	======================================
