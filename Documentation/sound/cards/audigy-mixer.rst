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
This control is used to attenuate samples from left and right front PCM FX-bus
accumulators. ALSA uses accumulators 8 and 9 for left and right front PCM 
samples for 5.1 playback. The result samples are forwarded to the front speakers.

name='PCM Surround Playback Volume',index=0
-------------------------------------------
This control is used to attenuate samples from left and right surround PCM FX-bus
accumulators. ALSA uses accumulators 2 and 3 for left and right surround PCM 
samples for 5.1 playback. The result samples are forwarded to the surround (rear)
speakers.

name='PCM Side Playback Volume',index=0
---------------------------------------
This control is used to attenuate samples from left and right side PCM FX-bus
accumulators. ALSA uses accumulators 14 and 15 for left and right side PCM
samples for 7.1 playback. The result samples are forwarded to the side speakers.

name='PCM Center Playback Volume',index=0
-----------------------------------------
This control is used to attenuate samples from center PCM FX-bus accumulator.
ALSA uses accumulator 6 for center PCM samples for 5.1 playback. The result
samples are forwarded to the center speaker.

name='PCM LFE Playback Volume',index=0
--------------------------------------
This control is used to attenuate sample for LFE PCM FX-bus accumulator. 
ALSA uses accumulator 7 for LFE PCM samples for 5.1 playback. The result
samples are forwarded to the subwoofer.

name='PCM Playback Volume',index=0
----------------------------------
This control is used to attenuate samples from left and right PCM FX-bus
accumulators. ALSA uses accumulators 0 and 1 for left and right PCM samples for
stereo playback. The result samples are forwarded to the front speakers.

name='PCM Capture Volume',index=0
---------------------------------
This control is used to attenuate samples from left and right PCM FX-bus
accumulators. ALSA uses accumulators 0 and 1 for left and right PCM samples for
stereo playback. The result is forwarded to the standard capture PCM device.

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

name='Mic Playback Volume',index=0
----------------------------------
This control is used to attenuate samples from left and right Mic input of
the AC97 codec. The result samples are forwarded to the virtual stereo mixer.

name='Mic Capture Volume',index=0
---------------------------------
This control is used to attenuate samples from left and right Mic input of
the AC97 codec. The result is forwarded to the standard capture PCM device.

The original samples are also forwarded to the Mic capture PCM device (device 1;
16bit/8KHz mono) without volume control.

name='Audigy CD Playback Volume',index=0
----------------------------------------
This control is used to attenuate samples from left and right IEC958 TTL
digital inputs (usually used by a CDROM drive). The result samples are
forwarded to the virtual stereo mixer.

name='Audigy CD Capture Volume',index=0
---------------------------------------
This control is used to attenuate samples from left and right IEC958 TTL
digital inputs (usually used by a CDROM drive). The result is forwarded
to the standard capture PCM device.

name='IEC958 Optical Playback Volume',index=0
---------------------------------------------
This control is used to attenuate samples from left and right IEC958 optical
digital input. The result samples are forwarded to the virtual stereo mixer.

name='IEC958 Optical Capture Volume',index=0
--------------------------------------------
This control is used to attenuate samples from left and right IEC958 optical
digital inputs. The result is forwarded to the standard capture PCM device.

name='Line2 Playback Volume',index=0
------------------------------------
This control is used to attenuate samples from left and right I2S ADC
inputs (on the AudigyDrive). The result samples are forwarded to the virtual
stereo mixer.

name='Line2 Capture Volume',index=1
-----------------------------------
This control is used to attenuate samples from left and right I2S ADC
inputs (on the AudigyDrive). The result is forwarded to the standard capture
PCM device.

name='Analog Mix Playback Volume',index=0
-----------------------------------------
This control is used to attenuate samples from left and right I2S ADC
inputs from Philips ADC. The result samples are forwarded to the virtual
stereo mixer. This contains mix from analog sources like CD, Line In, Aux, ....

name='Analog Mix Capture Volume',index=1
----------------------------------------
This control is used to attenuate samples from left and right I2S ADC
inputs Philips ADC. The result is forwarded to the standard capture PCM device.

name='Aux2 Playback Volume',index=0
-----------------------------------
This control is used to attenuate samples from left and right I2S ADC
inputs (on the AudigyDrive). The result samples are forwarded to the virtual
stereo mixer.

name='Aux2 Capture Volume',index=1
----------------------------------
This control is used to attenuate samples from left and right I2S ADC
inputs (on the AudigyDrive). The result is forwarded to the standard capture
PCM device.

name='Front Playback Volume',index=0
------------------------------------
This control is used to attenuate samples from the virtual stereo mixer.
The result samples are forwarded to the front speakers.

name='Surround Playback Volume',index=0
---------------------------------------
This control is used to attenuate samples from the virtual stereo mixer.
The result samples are forwarded to the surround (rear) speakers.

name='Side Playback Volume',index=0
-----------------------------------
This control is used to attenuate samples from the virtual stereo mixer.
The result samples are forwarded to the side speakers.

name='Center Playback Volume',index=0
-------------------------------------
This control is used to attenuate samples from the virtual stereo mixer.
The result samples are forwarded to the center speaker.

name='LFE Playback Volume',index=0
----------------------------------
This control is used to attenuate samples from the virtual stereo mixer.
The result samples are forwarded to the subwoofer.

name='Tone Control - Switch',index=0
------------------------------------
This control turns the tone control on or off. The samples forwarded to
the speaker outputs are affected.

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
This control is used to attenuate samples forwarded to the speaker outputs.

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

See sb-live-mixer.rst.
