===========================================
Sound Blaster Live mixer / default DSP code
===========================================


The EMU10K1 chips have a DSP part which can be programmed to support
various ways of sample processing, which is described here.
(This article does not deal with the overall functionality of the 
EMU10K1 chips. See the manuals section for further details.)

The ALSA driver programs this portion of chip by default code
(can be altered later) which offers the following functionality:


IEC958 (S/PDIF) raw PCM
=======================

This PCM device (it's the 3rd PCM device (index 2!) and first subdevice
(index 0) for a given card) allows to forward 48kHz, stereo, 16-bit
little endian streams without any modifications to the digital output
(coaxial or optical). The universal interface allows the creation of up
to 8 raw PCM devices operating at 48kHz, 16-bit little endian. It would
be easy to add support for multichannel devices to the current code,
but the conversion routines exist only for stereo (2-channel streams)
at the time. 

Look to tram_poke routines in lowlevel/emu10k1/emufx.c for more details.


Digital mixer controls
======================

These controls are built using the DSP instructions. They offer extended
functionality. Only the default built-in code in the ALSA driver is described
here. Note that the controls work as attenuators: the maximum value is the 
neutral position leaving the signal unchanged. Note that if the same destination
is mentioned in multiple controls, the signal is accumulated and can be clipped
(set to maximal or minimal value without checking for overflow).


Explanation of used abbreviations:

DAC
	digital to analog converter
ADC
	analog to digital converter
I2S
	one-way three wire serial bus for digital sound by Philips Semiconductors
	(this standard is used for connecting standalone D/A and A/D converters)
LFE
	low frequency effects (used as subwoofer signal)
AC97
	a chip containing an analog mixer, D/A and A/D converters
IEC958
	S/PDIF
FX-bus
	the EMU10K1 chip has an effect bus containing 16 accumulators.
	Each of the synthesizer voices can feed its output to these accumulators
	and the DSP microcontroller can operate with the resulting sum.


``name='Wave Playback Volume',index=0``
---------------------------------------
This control is used to attenuate samples from left and right PCM FX-bus
accumulators. ALSA uses accumulators 0 and 1 for left and right PCM samples.
The result samples are forwarded to the front DAC PCM slots of the AC97 codec.

``name='Wave Surround Playback Volume',index=0``
------------------------------------------------
This control is used to attenuate samples from left and right PCM FX-bus
accumulators. ALSA uses accumulators 0 and 1 for left and right PCM samples.
The result samples are forwarded to the rear I2S DACs. These DACs operates
separately (they are not inside the AC97 codec).

``name='Wave Center Playback Volume',index=0``
----------------------------------------------
This control is used to attenuate samples from left and right PCM FX-bus
accumulators. ALSA uses accumulators 0 and 1 for left and right PCM samples.
The result is mixed to mono signal (single channel) and forwarded to
the ??rear?? right DAC PCM slot of the AC97 codec.

``name='Wave LFE Playback Volume',index=0``
-------------------------------------------
This control is used to attenuate samples from left and right PCM FX-bus
accumulators. ALSA uses accumulators 0 and 1 for left and right PCM.
The result is mixed to mono signal (single channel) and forwarded to
the ??rear?? left DAC PCM slot of the AC97 codec.

``name='Wave Capture Volume',index=0``, ``name='Wave Capture Switch',index=0``
------------------------------------------------------------------------------
These controls are used to attenuate samples from left and right PCM FX-bus
accumulator. ALSA uses accumulators 0 and 1 for left and right PCM.
The result is forwarded to the ADC capture FIFO (thus to the standard capture
PCM device).

``name='Synth Playback Volume',index=0``
----------------------------------------
This control is used to attenuate samples from left and right MIDI FX-bus
accumulators. ALSA uses accumulators 4 and 5 for left and right MIDI samples.
The result samples are forwarded to the front DAC PCM slots of the AC97 codec.

``name='Synth Capture Volume',index=0``, ``name='Synth Capture Switch',index=0``
--------------------------------------------------------------------------------
These controls are used to attenuate samples from left and right MIDI FX-bus
accumulator. ALSA uses accumulators 4 and 5 for left and right MIDI samples.
The result is forwarded to the ADC capture FIFO (thus to the standard capture
PCM device).

``name='Surround Playback Volume',index=0``
-------------------------------------------
This control is used to attenuate samples from left and right rear PCM FX-bus
accumulators. ALSA uses accumulators 2 and 3 for left and right rear PCM samples.
The result samples are forwarded to the rear I2S DACs. These DACs operate
separately (they are not inside the AC97 codec).

``name='Surround Capture Volume',index=0``, ``name='Surround Capture Switch',index=0``
--------------------------------------------------------------------------------------
These controls are used to attenuate samples from left and right rear PCM FX-bus
accumulators. ALSA uses accumulators 2 and 3 for left and right rear PCM samples.
The result is forwarded to the ADC capture FIFO (thus to the standard capture
PCM device).

``name='Center Playback Volume',index=0``
-----------------------------------------
This control is used to attenuate sample for center PCM FX-bus accumulator.
ALSA uses accumulator 6 for center PCM sample. The result sample is forwarded
to the ??rear?? right DAC PCM slot of the AC97 codec.

``name='LFE Playback Volume',index=0``
--------------------------------------
This control is used to attenuate sample for center PCM FX-bus accumulator.
ALSA uses accumulator 6 for center PCM sample. The result sample is forwarded
to the ??rear?? left DAC PCM slot of the AC97 codec.

``name='AC97 Playback Volume',index=0``
---------------------------------------
This control is used to attenuate samples from left and right front ADC PCM slots
of the AC97 codec. The result samples are forwarded to the front DAC PCM
slots of the AC97 codec.

.. note::
  This control should be zero for the standard operations, otherwise
  a digital loopback is activated.


``name='AC97 Capture Volume',index=0``
--------------------------------------
This control is used to attenuate samples from left and right front ADC PCM slots
of the AC97 codec. The result is forwarded to the ADC capture FIFO (thus to
the standard capture PCM device).

.. note::
   This control should be 100 (maximal value), otherwise no analog
   inputs of the AC97 codec can be captured (recorded).

``name='IEC958 TTL Playback Volume',index=0``
---------------------------------------------
This control is used to attenuate samples from left and right IEC958 TTL
digital inputs (usually used by a CDROM drive). The result samples are
forwarded to the front DAC PCM slots of the AC97 codec.

``name='IEC958 TTL Capture Volume',index=0``
--------------------------------------------
This control is used to attenuate samples from left and right IEC958 TTL
digital inputs (usually used by a CDROM drive). The result samples are
forwarded to the ADC capture FIFO (thus to the standard capture PCM device).

``name='Zoom Video Playback Volume',index=0``
---------------------------------------------
This control is used to attenuate samples from left and right zoom video
digital inputs (usually used by a CDROM drive). The result samples are
forwarded to the front DAC PCM slots of the AC97 codec.

``name='Zoom Video Capture Volume',index=0``
--------------------------------------------
This control is used to attenuate samples from left and right zoom video
digital inputs (usually used by a CDROM drive). The result samples are
forwarded to the ADC capture FIFO (thus to the standard capture PCM device).

``name='IEC958 LiveDrive Playback Volume',index=0``
---------------------------------------------------
This control is used to attenuate samples from left and right IEC958 optical
digital input. The result samples are forwarded to the front DAC PCM slots
of the AC97 codec.

``name='IEC958 LiveDrive Capture Volume',index=0``
--------------------------------------------------
This control is used to attenuate samples from left and right IEC958 optical
digital inputs. The result samples are forwarded to the ADC capture FIFO
(thus to the standard capture PCM device).

``name='IEC958 Coaxial Playback Volume',index=0``
-------------------------------------------------
This control is used to attenuate samples from left and right IEC958 coaxial
digital inputs. The result samples are forwarded to the front DAC PCM slots
of the AC97 codec.

``name='IEC958 Coaxial Capture Volume',index=0``
------------------------------------------------
This control is used to attenuate samples from left and right IEC958 coaxial
digital inputs. The result samples are forwarded to the ADC capture FIFO
(thus to the standard capture PCM device).

``name='Line LiveDrive Playback Volume',index=0``, ``name='Line LiveDrive Playback Volume',index=1``
----------------------------------------------------------------------------------------------------
This control is used to attenuate samples from left and right I2S ADC
inputs (on the LiveDrive). The result samples are forwarded to the front
DAC PCM slots of the AC97 codec.

``name='Line LiveDrive Capture Volume',index=1``, ``name='Line LiveDrive Capture Volume',index=1``
--------------------------------------------------------------------------------------------------
This control is used to attenuate samples from left and right I2S ADC
inputs (on the LiveDrive). The result samples are forwarded to the ADC
capture FIFO (thus to the standard capture PCM device).

``name='Tone Control - Switch',index=0``
----------------------------------------
This control turns the tone control on or off. The samples for front, rear
and center / LFE outputs are affected.

``name='Tone Control - Bass',index=0``
--------------------------------------
This control sets the bass intensity. There is no neutral value!!
When the tone control code is activated, the samples are always modified.
The closest value to pure signal is 20.

``name='Tone Control - Treble',index=0``
----------------------------------------
This control sets the treble intensity. There is no neutral value!!
When the tone control code is activated, the samples are always modified.
The closest value to pure signal is 20.

``name='IEC958 Optical Raw Playback Switch',index=0``
-----------------------------------------------------
If this switch is on, then the samples for the IEC958 (S/PDIF) digital
output are taken only from the raw FX8010 PCM, otherwise standard front
PCM samples are taken.

``name='Headphone Playback Volume',index=1``
--------------------------------------------
This control attenuates the samples for the headphone output.

``name='Headphone Center Playback Switch',index=1``
---------------------------------------------------
If this switch is on, then the sample for the center PCM is put to the
left headphone output (useful for SB Live cards without separate center/LFE
output).

``name='Headphone LFE Playback Switch',index=1``
------------------------------------------------
If this switch is on, then the sample for the center PCM is put to the
right headphone output (useful for SB Live cards without separate center/LFE
output).


PCM stream related controls
===========================

``name='EMU10K1 PCM Volume',index 0-31``
----------------------------------------
Channel volume attenuation in range 0-0x1fffd. The middle value (no
attenuation) is default. The channel mapping for three values is
as follows:

* 0 - mono, default 0xffff (no attenuation)
* 1 - left, default 0xffff (no attenuation)
* 2 - right, default 0xffff (no attenuation)

``name='EMU10K1 PCM Send Routing',index 0-31``
----------------------------------------------
This control specifies the destination - FX-bus accumulators. There are
twelve values with this mapping:

*  0 -  mono, A destination (FX-bus 0-15), default 0
*  1 -  mono, B destination (FX-bus 0-15), default 1
*  2 -  mono, C destination (FX-bus 0-15), default 2
*  3 -  mono, D destination (FX-bus 0-15), default 3
*  4 -  left, A destination (FX-bus 0-15), default 0
*  5 -  left, B destination (FX-bus 0-15), default 1
*  6 -  left, C destination (FX-bus 0-15), default 2
*  7 -  left, D destination (FX-bus 0-15), default 3
*  8 - right, A destination (FX-bus 0-15), default 0
*  9 - right, B destination (FX-bus 0-15), default 1
* 10 - right, C destination (FX-bus 0-15), default 2
* 11 - right, D destination (FX-bus 0-15), default 3

Don't forget that it's illegal to assign a channel to the same FX-bus accumulator 
more than once (it means 0=0 && 1=0 is an invalid combination).
 
``name='EMU10K1 PCM Send Volume',index 0-31``
---------------------------------------------
It specifies the attenuation (amount) for given destination in range 0-255.
The channel mapping is following:

*  0 -  mono, A destination attn, default 255 (no attenuation)
*  1 -  mono, B destination attn, default 255 (no attenuation)
*  2 -  mono, C destination attn, default 0 (mute)
*  3 -  mono, D destination attn, default 0 (mute)
*  4 -  left, A destination attn, default 255 (no attenuation)
*  5 -  left, B destination attn, default 0 (mute)
*  6 -  left, C destination attn, default 0 (mute)
*  7 -  left, D destination attn, default 0 (mute)
*  8 - right, A destination attn, default 0 (mute)
*  9 - right, B destination attn, default 255 (no attenuation)
* 10 - right, C destination attn, default 0 (mute)
* 11 - right, D destination attn, default 0 (mute)



MANUALS/PATENTS
===============

ftp://opensource.creative.com/pub/doc
-------------------------------------

Note that the site is defunct, but the documents are available
from various other locations.

LM4545.pdf
	AC97 Codec
m2049.pdf
	The EMU10K1 Digital Audio Processor
hog63.ps
	FX8010 - A DSP Chip Architecture for Audio Effects


WIPO Patents
------------

WO 9901813 (A1)
	Audio Effects Processor with multiple asynchronous streams
	(Jan. 14, 1999)

WO 9901814 (A1)
	Processor with Instruction Set for Audio Effects (Jan. 14, 1999)

WO 9901953 (A1)
	Audio Effects Processor having Decoupled Instruction
        Execution and Audio Data Sequencing (Jan. 14, 1999)


US Patents (https://www.uspto.gov/)
-----------------------------------

US 5925841
	Digital Sampling Instrument employing cache memory (Jul. 20, 1999)

US 5928342
	Audio Effects Processor integrated on a single chip
        with a multiport memory onto which multiple asynchronous
        digital sound samples can be concurrently loaded
	(Jul. 27, 1999)

US 5930158
	Processor with Instruction Set for Audio Effects (Jul. 27, 1999)

US 6032235
	Memory initialization circuit (Tram) (Feb. 29, 2000)

US 6138207
	Interpolation looping of audio samples in cache connected to
        system bus with prioritization and modification of bus transfers
        in accordance with loop ends and minimum block sizes
	(Oct. 24, 2000)

US 6151670
	Method for conserving memory storage using a
        pool of  short term memory registers
	(Nov. 21, 2000)

US 6195715
	Interrupt control for multiple programs communicating with
        a common interrupt by associating programs to GP registers,
        defining interrupt register, polling GP registers, and invoking
        callback routine associated with defined interrupt register
	(Feb. 27, 2001)
