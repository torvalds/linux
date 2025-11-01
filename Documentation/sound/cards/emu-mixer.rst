==================================================
E-MU Digital Audio System mixer / default DSP code
==================================================

This document covers the E-MU 0404/1010/1212/1616/1820 PCI/PCI-e/CardBus
cards.

These cards use regular EMU10K2 (SoundBlaster Audigy) chips, but with an
alternative front-end geared towards semi-professional studio recording.

This document is based on audigy-mixer.rst.


Hardware compatibility
======================

The EMU10K2 chips have a very short capture FIFO, which makes recording
unreliable if the card's PCI bus requests are not handled with the
appropriate priority.
This is the case on more modern motherboards, where the PCI bus is only a
secondary peripheral, rather than the actual arbiter of device access.
In particular, I got recording glitches during simultaneous playback on an
Intel DP55 board (memory controller in the CPU), but had success with an
Intel DP45 board (memory controller in the north bridge).

The PCI Express variants of these cards (which have a PCI bridge on board,
but are otherwise identical) may be less problematic.


Driver capabilities
===================

This driver supports only 16-bit 44.1/48 kHz operation. The multi-channel
device (see emu10k1-jack.rst) additionally supports 24-bit capture.

A patchset to enhance the driver is available from `a GitHub repository
<https://github.com/ossilator/linux/tree/ossis-emu10k1>`_.
Its multi-channel device supports 24-bit for both playback and capture,
and also supports full 88.2/96/176.4/192 kHz operation.
It is not going to be upstreamed due to a fundamental disagreement about
what constitutes a good user experience.


Digital mixer controls
======================

Note that the controls work as attenuators: the maximum value is the neutral
position leaving the signal unchanged. Note that if the same destination is
mentioned in multiple controls, the signal is accumulated and can be clipped
(set to maximal or minimal value without checking for overflow).

Explanation of used abbreviations:

DAC
	digital to analog converter
ADC
	analog to digital converter
LFE
	low frequency effects (used as subwoofer signal)
IEC958
	S/PDIF
FX-bus
	the EMU10K2 chip has an effect bus containing 64 accumulators.
	Each of the synthesizer voices can feed its output to these accumulators
	and the DSP microcontroller can operate with the resulting sum.

name='Clock Source',index=0
---------------------------
This control allows switching the word clock between internally generated
44.1 or 48 kHz, or a number of external sources.

Note: the sources for the 1616 CardBus card are unclear. Please report your
findings.

name='Clock Fallback',index=0
-----------------------------
This control determines the internal clock which the card switches to when
the selected external clock source is/becomes invalid.

name='DAC1 0202 14dB PAD',index=0, etc.
---------------------------------------
Output attenuation controls. Not available on 0404 cards.

name='ADC1 14dB PAD 0202',index=0, etc.
---------------------------------------
Input attenuation controls. Not available on 0404 cards.

name='Optical Output Mode',index=0
----------------------------------
Switches the TOSLINK output port between S/PDIF and ADAT.
Not available on 0404 cards (fixed to S/PDIF).

name='Optical Input Mode',index=0
---------------------------------
Switches the TOSLINK input port between S/PDIF and ADAT.
Not available on 0404 cards (fixed to S/PDIF).

name='PCM Front Playback Volume',index=0
----------------------------------------
This control is used to attenuate samples from left and right front PCM FX-bus
accumulators. ALSA uses accumulators 8 and 9 for left and right front PCM
samples for 5.1 playback. The result samples are forwarded to the DSP 0 & 1
playback channels.

name='PCM Surround Playback Volume',index=0
-------------------------------------------
This control is used to attenuate samples from left and right surround PCM FX-bus
accumulators. ALSA uses accumulators 2 and 3 for left and right surround PCM
samples for 5.1 playback. The result samples are forwarded to the DSP 2 & 3
playback channels.

name='PCM Side Playback Volume',index=0
---------------------------------------
This control is used to attenuate samples from left and right side PCM FX-bus
accumulators. ALSA uses accumulators 14 and 15 for left and right side PCM
samples for 7.1 playback. The result samples are forwarded to the DSP 6 & 7
playback channels.

name='PCM Center Playback Volume',index=0
-----------------------------------------
This control is used to attenuate samples from the center PCM FX-bus accumulator.
ALSA uses accumulator 6 for center PCM samples for 5.1 playback. The result samples
are forwarded to the DSP 4 playback channel.

name='PCM LFE Playback Volume',index=0
--------------------------------------
This control is used to attenuate samples from the LFE PCM FX-bus accumulator.
ALSA uses accumulator 7 for LFE PCM samples for 5.1 playback. The result samples
are forwarded to the DSP 5 playback channel.

name='PCM Playback Volume',index=0
----------------------------------
This control is used to attenuate samples from left and right PCM FX-bus
accumulators. ALSA uses accumulators 0 and 1 for left and right PCM samples for
stereo playback. The result samples are forwarded to the virtual stereo mixer.

name='PCM Capture Volume',index=0
---------------------------------
This control is used to attenuate samples from left and right PCM FX-bus
accumulators. ALSA uses accumulators 0 and 1 for left and right PCM.
The result is forwarded to the standard capture PCM device.

name='Music Playback Volume',index=0
------------------------------------
This control is used to attenuate samples from left and right MIDI FX-bus
accumulators. ALSA uses accumulators 4 and 5 for left and right MIDI samples.
The result samples are forwarded to the virtual stereo mixer.

name='Music Capture Volume',index=0
-----------------------------------
These controls are used to attenuate samples from left and right MIDI FX-bus
accumulator. ALSA uses accumulators 4 and 5 for left and right MIDI samples.
The result is forwarded to the standard capture PCM device.

name='Front Playback Volume',index=0
------------------------------------
This control is used to attenuate samples from the virtual stereo mixer.
The result samples are forwarded to the DSP 0 & 1 playback channels.

name='Surround Playback Volume',index=0
---------------------------------------
This control is used to attenuate samples from the virtual stereo mixer.
The result samples are forwarded to the DSP 2 & 3 playback channels.

name='Side Playback Volume',index=0
-----------------------------------
This control is used to attenuate samples from the virtual stereo mixer.
The result samples are forwarded to the DSP 6 & 7 playback channels.

name='Center Playback Volume',index=0
-------------------------------------
This control is used to attenuate samples from the virtual stereo mixer.
The result samples are forwarded to the DSP 4 playback channel.

name='LFE Playback Volume',index=0
----------------------------------
This control is used to attenuate samples from the virtual stereo mixer.
The result samples are forwarded to the DSP 5 playback channel.

name='Tone Control - Switch',index=0
------------------------------------
This control turns the tone control on or off. The samples forwarded to
the DSP playback channels are affected.

name='Tone Control - Bass',index=0
----------------------------------
This control sets the bass intensity. There is no neutral value!!
When the tone control code is activated, the samples are always modified.
The closest value to pure signal is 20.

name='Tone Control - Treble',index=0
------------------------------------
This control sets the treble intensity. There is no neutral value!!
When the tone control code is activated, the samples are always modified.
The closest value to pure signal is 20.

name='Master Playback Volume',index=0
-------------------------------------
This control is used to attenuate samples for all DSP playback channels.

name='EMU Capture Volume',index=0
----------------------------------
This control is used to attenuate samples from the DSP 0 & 1 capture channels.
The result is forwarded to the standard capture PCM device.

name='DAC Left',index=0, etc.
-----------------------------
Select the source for the given physical audio output. These may be physical
inputs, playback channels (DSP xx, specified as a decimal number), or silence.

name='DSP x',index=0
--------------------
Select the source for the given capture channel (specified as a hexadecimal
digit). Same options as for the physical audio outputs.


PCM stream related controls
===========================

These controls are described in audigy-mixer.rst.


MANUALS/PATENTS
===============

See sb-live-mixer.rst.
