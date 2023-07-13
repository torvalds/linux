=============================================
Sound Blaster Audigy mixer / default DSP code
=============================================

This is based on sb-live-mixer.rst.

The EMU10K2 chips have a DSP part which can be programmed to support 
various ways of sample processing, which is described here.
(This article does not deal with the overall functionality of the 
EMU10K2 chips. See the manuals section for further details.)

The ALSA driver programs this portion of chip by default code
(can be altered later) which offers the following functionality:


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
	the EMU10K2 chip has an effect bus containing 64 accumulators.
	Each of the synthesizer voices can feed its output to these accumulators
	and the DSP microcontroller can operate with the resulting sum.

name='PCM Front Playback Volume',index=0
----------------------------------------
This control is used to attenuate samples for left and right front PCM FX-bus
accumulators. ALSA uses accumulators 8 and 9 for left and right front PCM 
samples for 5.1 playback. The result samples are forwarded to the front DAC PCM 
slots of the Philips DAC.

name='PCM Surround Playback Volume',index=0
-------------------------------------------
This control is used to attenuate samples for left and right surround PCM FX-bus
accumulators. ALSA uses accumulators 2 and 3 for left and right surround PCM 
samples for 5.1 playback. The result samples are forwarded to the surround DAC PCM 
slots of the Philips DAC.

name='PCM Center Playback Volume',index=0
-----------------------------------------
This control is used to attenuate samples for center PCM FX-bus accumulator.
ALSA uses accumulator 6 for center PCM sample for 5.1 playback. The result sample
is forwarded to the center DAC PCM slot of the Philips DAC.

name='PCM LFE Playback Volume',index=0
--------------------------------------
This control is used to attenuate sample for LFE PCM FX-bus accumulator. 
ALSA uses accumulator 7 for LFE PCM sample for 5.1 playback. The result sample 
is forwarded to the LFE DAC PCM slot of the Philips DAC.

name='PCM Playback Volume',index=0
----------------------------------
This control is used to attenuate samples for left and right PCM FX-bus
accumulators. ALSA uses accumulators 0 and 1 for left and right PCM samples for
stereo playback. The result samples are forwarded to the front DAC PCM slots 
of the Philips DAC.

name='PCM Capture Volume',index=0
---------------------------------
This control is used to attenuate samples for left and right PCM FX-bus
accumulator. ALSA uses accumulators 0 and 1 for left and right PCM.
The result is forwarded to the ADC capture FIFO (thus to the standard capture
PCM device).

name='Music Playback Volume',index=0
------------------------------------
This control is used to attenuate samples for left and right MIDI FX-bus
accumulators. ALSA uses accumulators 4 and 5 for left and right MIDI samples.
The result samples are forwarded to the front DAC PCM slots of the AC97 codec.

name='Music Capture Volume',index=0
-----------------------------------
These controls are used to attenuate samples for left and right MIDI FX-bus
accumulator. ALSA uses accumulators 4 and 5 for left and right PCM.
The result is forwarded to the ADC capture FIFO (thus to the standard capture
PCM device).

name='Mic Playback Volume',index=0
----------------------------------
This control is used to attenuate samples for left and right Mic input.
For Mic input is used AC97 codec. The result samples are forwarded to 
the front DAC PCM slots of the Philips DAC. Samples are forwarded to Mic
capture FIFO (device 1 - 16bit/8KHz mono) too without volume control.

name='Mic Capture Volume',index=0
---------------------------------
This control is used to attenuate samples for left and right Mic input.
The result is forwarded to the ADC capture FIFO (thus to the standard capture
PCM device).

name='Audigy CD Playback Volume',index=0
----------------------------------------
This control is used to attenuate samples from left and right IEC958 TTL
digital inputs (usually used by a CDROM drive). The result samples are
forwarded to the front DAC PCM slots of the Philips DAC.

name='Audigy CD Capture Volume',index=0
---------------------------------------
This control is used to attenuate samples from left and right IEC958 TTL
digital inputs (usually used by a CDROM drive). The result samples are
forwarded to the ADC capture FIFO (thus to the standard capture PCM device).

name='IEC958 Optical Playback Volume',index=0
---------------------------------------------
This control is used to attenuate samples from left and right IEC958 optical
digital input. The result samples are forwarded to the front DAC PCM slots
of the Philips DAC.

name='IEC958 Optical Capture Volume',index=0
--------------------------------------------
This control is used to attenuate samples from left and right IEC958 optical
digital inputs. The result samples are forwarded to the ADC capture FIFO
(thus to the standard capture PCM device).

name='Line2 Playback Volume',index=0
------------------------------------
This control is used to attenuate samples from left and right I2S ADC
inputs (on the AudigyDrive). The result samples are forwarded to the front
DAC PCM slots of the Philips DAC.

name='Line2 Capture Volume',index=1
-----------------------------------
This control is used to attenuate samples from left and right I2S ADC
inputs (on the AudigyDrive). The result samples are forwarded to the ADC
capture FIFO (thus to the standard capture PCM device).

name='Analog Mix Playback Volume',index=0
-----------------------------------------
This control is used to attenuate samples from left and right I2S ADC
inputs from Philips ADC. The result samples are forwarded to the front
DAC PCM slots of the Philips DAC. This contains mix from analog sources
like CD, Line In, Aux, ....

name='Analog Mix Capture Volume',index=1
----------------------------------------
This control is used to attenuate samples from left and right I2S ADC
inputs Philips ADC. The result samples are forwarded to the ADC
capture FIFO (thus to the standard capture PCM device).

name='Aux2 Playback Volume',index=0
-----------------------------------
This control is used to attenuate samples from left and right I2S ADC
inputs (on the AudigyDrive). The result samples are forwarded to the front
DAC PCM slots of the Philips DAC.

name='Aux2 Capture Volume',index=1
----------------------------------
This control is used to attenuate samples from left and right I2S ADC
inputs (on the AudigyDrive). The result samples are forwarded to the ADC
capture FIFO (thus to the standard capture PCM device).

name='Front Playback Volume',index=0
------------------------------------
All stereo signals are mixed together and mirrored to surround, center and LFE.
This control is used to attenuate samples for left and right front speakers of
this mix.

name='Surround Playback Volume',index=0
---------------------------------------
All stereo signals are mixed together and mirrored to surround, center and LFE.
This control is used to attenuate samples for left and right surround speakers of
this mix.

name='Center Playback Volume',index=0
-------------------------------------
All stereo signals are mixed together and mirrored to surround, center and LFE.
This control is used to attenuate sample for center speaker of this mix.

name='LFE Playback Volume',index=0
----------------------------------
All stereo signals are mixed together and mirrored to surround, center and LFE.
This control is used to attenuate sample for LFE speaker of this mix.

name='Tone Control - Switch',index=0
------------------------------------
This control turns the tone control on or off. The samples for front, rear
and center / LFE outputs are affected.

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
This control is used to attenuate samples for front, surround, center and 
LFE outputs.

name='IEC958 Optical Raw Playback Switch',index=0
-------------------------------------------------
If this switch is on, then the samples for the IEC958 (S/PDIF) digital
output are taken only from the raw iec958 ALSA PCM device (which uses
accumulators 20 and 21 for left and right PCM by default).


PCM stream related controls
===========================

name='EMU10K1 PCM Volume',index 0-31
------------------------------------
Channel volume attenuation in range 0-0x1fffd. The middle value (no
attenuation) is default. The channel mapping for three values is
as follows:

* 0 - mono, default 0xffff (no attenuation)
* 1 - left, default 0xffff (no attenuation)
* 2 - right, default 0xffff (no attenuation)

name='EMU10K1 PCM Send Routing',index 0-31
------------------------------------------
This control specifies the destination - FX-bus accumulators. There are 24
values in this mapping:

*  0 -  mono, A destination (FX-bus 0-63), default 0
*  1 -  mono, B destination (FX-bus 0-63), default 1
*  2 -  mono, C destination (FX-bus 0-63), default 2
*  3 -  mono, D destination (FX-bus 0-63), default 3
*  4 -  mono, E destination (FX-bus 0-63), default 4
*  5 -  mono, F destination (FX-bus 0-63), default 5
*  6 -  mono, G destination (FX-bus 0-63), default 6
*  7 -  mono, H destination (FX-bus 0-63), default 7
*  8 -  left, A destination (FX-bus 0-63), default 0
*  9 -  left, B destination (FX-bus 0-63), default 1
* 10 -  left, C destination (FX-bus 0-63), default 2
* 11 -  left, D destination (FX-bus 0-63), default 3
* 12 -  left, E destination (FX-bus 0-63), default 4
* 13 -  left, F destination (FX-bus 0-63), default 5
* 14 -  left, G destination (FX-bus 0-63), default 6
* 15 -  left, H destination (FX-bus 0-63), default 7
* 16 - right, A destination (FX-bus 0-63), default 0
* 17 - right, B destination (FX-bus 0-63), default 1
* 18 - right, C destination (FX-bus 0-63), default 2
* 19 - right, D destination (FX-bus 0-63), default 3
* 20 - right, E destination (FX-bus 0-63), default 4
* 21 - right, F destination (FX-bus 0-63), default 5
* 22 - right, G destination (FX-bus 0-63), default 6
* 23 - right, H destination (FX-bus 0-63), default 7

Don't forget that it's illegal to assign a channel to the same FX-bus accumulator 
more than once (it means 0=0 && 1=0 is an invalid combination).
 
name='EMU10K1 PCM Send Volume',index 0-31
-----------------------------------------
It specifies the attenuation (amount) for given destination in range 0-255.
The channel mapping is following:

*  0 -  mono, A destination attn, default 255 (no attenuation)
*  1 -  mono, B destination attn, default 255 (no attenuation)
*  2 -  mono, C destination attn, default 0 (mute)
*  3 -  mono, D destination attn, default 0 (mute)
*  4 -  mono, E destination attn, default 0 (mute)
*  5 -  mono, F destination attn, default 0 (mute)
*  6 -  mono, G destination attn, default 0 (mute)
*  7 -  mono, H destination attn, default 0 (mute)
*  8 -  left, A destination attn, default 255 (no attenuation)
*  9 -  left, B destination attn, default 0 (mute)
* 10 -  left, C destination attn, default 0 (mute)
* 11 -  left, D destination attn, default 0 (mute)
* 12 -  left, E destination attn, default 0 (mute)
* 13 -  left, F destination attn, default 0 (mute)
* 14 -  left, G destination attn, default 0 (mute)
* 15 -  left, H destination attn, default 0 (mute)
* 16 - right, A destination attn, default 0 (mute)
* 17 - right, B destination attn, default 255 (no attenuation)
* 18 - right, C destination attn, default 0 (mute)
* 19 - right, D destination attn, default 0 (mute)
* 20 - right, E destination attn, default 0 (mute)
* 21 - right, F destination attn, default 0 (mute)
* 22 - right, G destination attn, default 0 (mute)
* 23 - right, H destination attn, default 0 (mute)



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
