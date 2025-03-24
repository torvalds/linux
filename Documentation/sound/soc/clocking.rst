==============
Audio Clocking
==============

This text describes the audio clocking terms in ASoC and digital audio in
general. Note: Audio clocking can be complex!


Master Clock
------------

Every audio subsystem is driven by a master clock (sometimes referred to as MCLK
or SYSCLK). This audio master clock can be derived from a number of sources
(e.g. crystal, PLL, CPU clock) and is responsible for producing the correct
audio playback and capture sample rates.

Some master clocks (e.g. PLLs and CPU based clocks) are configurable in that
their speed can be altered by software (depending on the system use and to save
power). Other master clocks are fixed at a set frequency (i.e. crystals).


DAI Clocks
----------
The Digital Audio Interface is usually driven by a Bit Clock (often referred to
as BCLK). This clock is used to drive the digital audio data across the link
between the codec and CPU.

The DAI also has a frame clock to signal the start of each audio frame. This
clock is sometimes referred to as LRC (left right clock) or FRAME. This clock
runs at exactly the sample rate (LRC = Rate).

Bit Clock can be generated as follows:-

- BCLK = MCLK / x, or
- BCLK = LRC * x, or
- BCLK = LRC * Channels * Word Size

This relationship depends on the codec or SoC CPU in particular. In general
it is best to configure BCLK to the lowest possible speed (depending on your
rate, number of channels and word size) to save on power.

It is also desirable to use the codec (if possible) to drive (or master) the
audio clocks as it usually gives more accurate sample rates than the CPU.

ASoC provided clock APIs
------------------------

.. kernel-doc:: sound/soc/soc-dai.c
   :identifiers: snd_soc_dai_set_sysclk

.. kernel-doc:: sound/soc/soc-dai.c
   :identifiers: snd_soc_dai_set_clkdiv

.. kernel-doc:: sound/soc/soc-dai.c
   :identifiers: snd_soc_dai_set_pll

.. kernel-doc:: sound/soc/soc-dai.c
   :identifiers: snd_soc_dai_set_bclk_ratio
