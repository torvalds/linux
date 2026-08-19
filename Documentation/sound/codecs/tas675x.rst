.. SPDX-License-Identifier: GPL-2.0

======================================
TAS675x Codec Mixer Controls
======================================

This document describes the ALSA mixer controls for the TAS675x
4-channel amplifier driver.

For device tree bindings, see:
Documentation/devicetree/bindings/sound/ti,tas67524.yaml

DSP Signal Path Mode
====================

DSP Signal Path Mode
--------------------

:Description: Signal processing mode selection.
:Type:        Enumerated
:Default:     Normal
:Options:     Normal, LLP, FFLP
:Register:    0x32 bits [1:0]

Normal
  Full DSP with all features available.

LLP (Low Latency Path)
  Bypasses DSP processing. DSP protection features (Thermal Foldback,
  PVDD Foldback, Clip Detect) and Real-Time Load Diagnostics unavailable.

FFLP (Full Feature Low Latency Path)
  Reduced latency. Real-Time Load Diagnostics unavailable.

The following controls are unavailable in LLP mode:
``Thermal Foldback Switch``, ``PVDD Foldback Switch``,
``DC Blocker Bypass Switch``, ``Clip Detect Switch``, ``Audio SDOUT Switch``.

The following controls require Normal mode (unavailable in FFLP and LLP):
``CHx RTLDG Switch``, ``RTLDG Clip Mask Switch``, ``ISENSE Calibration Switch``,
``RTLDG Open Load Threshold``, ``RTLDG Short Load Threshold``,
``CHx RTLDG Impedance``.

Volume Controls
===============

Analog Playback Volume
----------------------

:Description: Analog output gain for all channels (CH1/CH2 and CH3/CH4 pairs).
:Type:        Volume (TLV)
:Default:     0 dB
:Range:       -15.5 dB to 0 dB (0.5 dB steps)
:Register:    0x4A (CH1/CH2), 0x4B (CH3/CH4)

Analog Gain Ramp Step
---------------------

:Description: Anti-pop ramp step duration for analog gain transitions.
:Type:        Enumerated
:Default:     15us
:Options:     15us, 60us, 200us, 400us
:Register:    0x4E bits [3:2]

CHx Digital Playback Volume
---------------------------

:Description: Per-channel digital volume control (x = 1, 2, 3, 4).
:Type:        Volume (TLV)
:Default:     0 dB
:Range:       -103 dB to 0 dB (0.5 dB steps)
:Bounds:      0x30 (min/mute) to 0xFF (max)
:Register:    0x40 (CH1), 0x41 (CH2), 0x42 (CH3), 0x43 (CH4)

Volume Ramp Down Rate
---------------------

:Description: Update frequency during mute transition.
:Type:        Enumerated
:Default:     16 FS
:Options:     4 FS, 16 FS, 32 FS, Instant
:Register:    0x44 bits [7:6]

Volume Ramp Down Step
---------------------

:Description: dB change per update during mute.
:Type:        Enumerated
:Default:     0.5dB
:Options:     4dB, 2dB, 1dB, 0.5dB
:Register:    0x44 bits [5:4]

Volume Ramp Up Rate
-------------------

:Description: Update frequency during unmute transition.
:Type:        Enumerated
:Default:     16 FS
:Options:     4 FS, 16 FS, 32 FS, Instant
:Register:    0x44 bits [3:2]

Volume Ramp Up Step
-------------------

:Description: dB change per update during unmute.
:Type:        Enumerated
:Default:     0.5dB
:Options:     4dB, 2dB, 1dB, 0.5dB
:Register:    0x44 bits [1:0]

CH1/2 Volume Combine
--------------------

:Description: Links digital volume controls for CH1 and CH2.
:Type:        Enumerated
:Default:     Independent
:Options:     Independent, CH2 follows CH1, CH1 follows CH2
:Register:    0x46 bits [1:0]

CH3/4 Volume Combine
--------------------

:Description: Links digital volume controls for CH3 and CH4.
:Type:        Enumerated
:Default:     Independent
:Options:     Independent, CH4 follows CH3, CH3 follows CH4
:Register:    0x46 bits [3:2]

Auto Mute & Silence Detection
==============================

CHx Auto Mute Switch
--------------------

:Description: Enables automatic muting on zero-signal detection (x = 1, 2, 3, 4).
:Type:        Boolean Switch
:Default:     Disabled (0)
:Register:    0x47 bit [0] (CH1), bit [1] (CH2), bit [2] (CH3), bit [3] (CH4)

Auto Mute Combine Switch
------------------------

:Description: Coordinated muting behaviour across all channels.
:Type:        Boolean Switch
:Default:     Disabled (0)
:Behavior:    Disabled: channels mute independently when their signal is zero.
              Enabled: all channels mute together only when all detect zero
              signal; unmute when any channel has non-zero signal.
:Register:    0x47 bit [4]

CHx Auto Mute Time
------------------

:Description: Duration of zero signal before muting triggers (x = 1, 2, 3, 4).
:Type:        Enumerated
:Default:     11.5ms
:Options:     11.5ms, 53ms, 106.5ms, 266.5ms, 535ms, 1065ms, 2665ms, 5330ms
:Register:    0x48 bits [7:4] (CH1), bits [3:0] (CH2),
              0x49 bits [7:4] (CH3), bits [3:0] (CH4)
:Note:        Values are at 96 kHz. At 48 kHz, times are doubled.

Clock & EMI Management
======================

Spread Spectrum Mode
--------------------

:Description: Frequency dithering mode to reduce peak EMI.
:Type:        Enumerated
:Default:     Disabled
:Options:     Disabled, Triangle, Random, Triangle and Random
:Register:    0x61 bits [1:0]

SS Triangle Range
-----------------

:Description: Frequency deviation range for Triangle spread spectrum.
:Type:        Enumerated
:Default:     6.5%
:Options:     6.5%, 13.5%, 5%, 10%
:Register:    0x62 bits [1:0]
:Note:        Applies only when Spread Spectrum Mode includes Triangle.

SS Random Range
---------------

:Description: Frequency deviation range for Random spread spectrum.
:Type:        Enumerated
:Default:     0.83%
:Options:     0.83%, 2.50%, 5.83%, 12.50%, 25.83%
:Register:    0x62 bits [6:4]
:Note:        Applies only when Spread Spectrum Mode includes Random.

SS Random Dwell Range
---------------------

:Description: Dwell time range for Random spread spectrum (FSS = spread
              spectrum modulation frequency).
:Type:        Enumerated
:Default:     1/FSS to 2/FSS
:Options:     1/FSS to 2/FSS, 1/FSS to 4/FSS, 1/FSS to 8/FSS, 1/FSS to 15/FSS
:Register:    0x62 bits [3:2]
:Note:        Applies only when Spread Spectrum Mode includes Random.

SS Triangle Dwell Min
---------------------

:Description: Minimum dwell time at Triangle spread spectrum frequency extremes.
:Type:        Integer
:Default:     0
:Range:       0 to 15 (0 = feature disabled)
:Register:    0x66 bits [7:4]
:Note:        Counts in FSS clock cycles. The modulator holds the extreme
              frequency for at least this many FSS cycles before reversing.
              When Dwell Min equals Dwell Max, the dwell feature is inactive.
              For FSS values at each PWM frequency refer to the "Spread
              Spectrum" section of the TRM.

SS Triangle Dwell Max
---------------------

:Description: Maximum dwell time at Triangle spread spectrum frequency extremes.
:Type:        Integer
:Default:     0
:Range:       0 to 15 (0 = feature disabled)
:Register:    0x66 bits [3:0]
:Note:        Counts in FSS clock cycles. Must be >= Dwell Min. When Dwell Max
              equals Dwell Min, the dwell feature is inactive.

Hardware Protection
===================

OTSD Auto Recovery Switch
--------------------------

:Description: Enables automatic recovery from over-temperature shutdown.
:Type:        Boolean Switch
:Default:     Disabled (0)
:Register:    0x8F bit [1]
:Note:        When disabled, manual fault clearing is required after OTSD events.

Overcurrent Limit Level
-----------------------

:Description: Current-limit trip point sensitivity.
:Type:        Enumerated
:Default:     Level 4
:Options:     Level 4, Level 3, Level 2, Level 1
:Register:    0x55 bits [1:0]
:Note:        Level 4 is the least sensitive (highest trip current); Level 1 is
              the most sensitive. The exact ILIM values depend on operating
              conditions (PVDD voltage, switching frequency, and temperature).
              Refer to the Electrical Characteristics table and the
              "Overcurrent Limit (Cycle-By-Cycle)" section of the TRM.

CHx OTW Threshold
-----------------

:Description: Over-temperature warning threshold per channel (x = 1, 2, 3, 4).
:Type:        Enumerated
:Default:     >95C
:Options:     Disabled, >95C, >110C, >125C, >135C, >145C, >155C, >165C
:Register:    0xE2 bits [6:4] (CH1), bits [2:0] (CH2),
              0xE3 bits [6:4] (CH3), bits [2:0] (CH4)

Temperature and Voltage Monitoring
===================================

PVDD Sense
----------

:Description: Supply voltage sense register.
:Type:        Integer (read-only)
:Range:       0 to 255
:Conversion:  value × 0.19 V
:Register:    0x74

Global Temperature
------------------

:Description: Global die temperature sense register.
:Type:        Integer (read-only)
:Range:       0 to 255
:Conversion:  value × 2.19 K; subtract 273.15 for °C
:Register:    0x75

CHx Temperature Range
---------------------

:Description: Per-channel coarse temperature range indicator (x = 1, 2, 3, 4).
:Type:        Integer (read-only)
:Range:       0 to 7
:Mapping:     0 = <95 °C, 1 = 95–110 °C, 2 = 110–125 °C, 3 = 125–135 °C,
              4 = 135–145 °C, 5 = 145–155 °C, 6 = 155–165 °C, 7 = >165 °C
:Register:    0xBB bits [2:0] (CH1), bits [5:3] (CH2),
              0xBC bits [2:0] (CH3), bits [5:3] (CH4)

Load Diagnostics
================

The TAS675x provides three load diagnostic modes:

DC Load Diagnostics (DC LDG)
  Measures DC resistance to detect S2G (short-to-ground), S2P
  (short-to-power), OL (open load), and SL (shorted load) faults.

AC Load Diagnostics (AC LDG)
  Measures complex AC impedance at a configurable frequency. Detects
  capacitive loads and tweeter configurations.

Real-Time Load Diagnostics (RTLDG)
  Monitors impedance continuously during playback using a pilot tone.
  Normal DSP mode only, at 48 kHz or 96 kHz.

Fast Boot Mode
--------------

By default the device runs DC load diagnostics at initialization before
accepting audio. Setting ``ti,fast-boot`` in the device tree bypasses this
initial diagnostic run for faster startup. Automatic diagnostics after
fault recovery remain enabled.

DC Load Diagnostics
-------------------

The ``CHx DC LDG Report`` 4-bit fault field uses the following encoding:

  ======  ===========  ===================================================
  Bit     Fault        Description
  ======  ===========  ===================================================
  [3]     S2G          Short-to-Ground
  [2]     S2P          Short-to-Power
  [1]     OL           Open Load
  [0]     SL           Shorted Load
  ======  ===========  ===================================================

DC LDG Trigger
~~~~~~~~~~~~~~

:Description: Triggers manual DC load diagnostics on all channels.
:Type:        Boolean (write-only)
:Note:        Returns -EBUSY if any DAI stream (playback or capture) is active.
              The driver manages all channel state transitions. Blocks until
              diagnostics complete or time out (300 ms).

DC LDG Auto Diagnostics Switch
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

:Description: Enables automatic DC diagnostics after fault recovery.
:Type:        Boolean Switch
:Default:     Enabled (1)
:Register:    0xB0 bit [0]
:Note:        Active-low, when enabled, affected channels re-run diagnostics after
              fault recovery and retry approximately every 750 ms until resolved.

CHx LO LDG Switch
~~~~~~~~~~~~~~~~~

:Description: Enables line output load detection per channel (x = 1, 2, 3, 4).
:Type:        Boolean Switch
:Default:     Disabled (0)
:Register:    0xB1 bit [3] (CH1), bit [2] (CH2), bit [1] (CH3), bit [0] (CH4)
:Note:        When enabled and DC diagnostics report OL, the device tests for
              a high-impedance line output load.

DC LDG SLOL Ramp Time
~~~~~~~~~~~~~~~~~~~~~

:Description: Voltage ramp time for shorted-load and open-load detection.
:Type:        Enumerated
:Default:     15 ms
:Options:     15 ms, 30 ms, 10 ms, 20 ms
:Register:    0xB2 bits [7:6]

DC LDG SLOL Settling Time
~~~~~~~~~~~~~~~~~~~~~~~~~~

:Description: Settling time for shorted-load and open-load detection.
:Type:        Enumerated
:Default:     10 ms
:Options:     10 ms, 5 ms, 20 ms, 15 ms
:Register:    0xB2 bits [5:4]

DC LDG S2PG Ramp Time
~~~~~~~~~~~~~~~~~~~~~

:Description: Voltage ramp time for short-to-power and short-to-ground detection.
:Type:        Enumerated
:Default:     5 ms
:Options:     5 ms, 2.5 ms, 10 ms, 15 ms
:Register:    0xB2 bits [3:2]

DC LDG S2PG Settling Time
~~~~~~~~~~~~~~~~~~~~~~~~~~

:Description: Settling time for short-to-power and short-to-ground detection.
:Type:        Enumerated
:Default:     10 ms
:Options:     10 ms, 5 ms, 20 ms, 30 ms
:Register:    0xB2 bits [1:0]

CHx DC LDG SL Threshold
~~~~~~~~~~~~~~~~~~~~~~~~

:Description: Shorted-load detection threshold per channel (x = 1, 2, 3, 4).
:Type:        Enumerated
:Default:     1 Ohm
:Options:     0.5 Ohm, 1 Ohm, 1.5 Ohm, 2 Ohm, 2.5 Ohm,
              3 Ohm, 3.5 Ohm, 4 Ohm, 4.5 Ohm, 5 Ohm
:Register:    0xB3 bits [7:4] (CH1), bits [3:0] (CH2),
              0xB4 bits [7:4] (CH3), bits [3:0] (CH4)

DC LDG Result
~~~~~~~~~~~~~

:Description: Overall DC diagnostic result register.
:Type:        Integer (read-only)
:Range:       0x00 to 0xFF
:Register:    0xC2
:Bit Encoding:

  ========  =====================================================
  Bits      Description
  ========  =====================================================
  [7:4]     Line output detection result, one bit per channel
  [3:0]     DC diagnostic pass/fail per channel (1=pass, 0=fail)
  ========  =====================================================

CHx DC LDG Report
~~~~~~~~~~~~~~~~~

:Description: DC diagnostic fault status per channel (x = 1, 2, 3, 4).
:Type:        Integer (read-only)
:Range:       0x0 to 0xF
:Register:    0xC0 bits [7:4] (CH1), bits [3:0] (CH2),
              0xC1 bits [7:4] (CH3), bits [3:0] (CH4)
:Note:        See fault bit encoding table at the start of this section.

CHx LO LDG Report
~~~~~~~~~~~~~~~~~

:Description: Line output load detection result per channel (x = 1, 2, 3, 4).
:Type:        Boolean (read-only)
:Values:      0 = not detected, 1 = line output load detected
:Register:    0xC2 bit [7] (CH1), bit [6] (CH2), bit [5] (CH3), bit [4] (CH4)

CHx DC Resistance
~~~~~~~~~~~~~~~~~

:Description: Measured DC load resistance per channel (x = 1, 2, 3, 4).
:Type:        Float (read-only, displayed in ohms)
:Resolution:  0.1 ohm per code (10-bit value)
:Range:       0.0 to 102.3 ohms
:Register:    0xD9 bits [7:6]/[5:4]/[3:2]/[1:0] (MSB for CH1–CH4),
              0xDA (CH1 LSB), 0xDB (CH2 LSB), 0xDC (CH3 LSB), 0xDD (CH4 LSB)

AC Load Diagnostics
-------------------

AC LDG Trigger
~~~~~~~~~~~~~~

:Description: Triggers AC impedance measurement on all channels.
:Type:        Boolean (write-only)
:Note:        Returns -EBUSY if any DAI stream (playback or capture) is active.
              The driver transitions all channels to SLEEP state before starting
              the measurement. Blocks until diagnostics complete or time out.

AC LDG Gain
~~~~~~~~~~~

:Description: Measurement resolution for AC diagnostics.
:Type:        Boolean Switch
:Default:     1 (Gain 8)
:Values:      0 = 0.8 ohm/code (Gain 1), 1 = 0.1 ohm/code (Gain 8)
:Register:    0xB5 bit [4]
:Note:        Gain 8 recommended for load impedances below 8 ohms.

AC LDG Test Frequency
~~~~~~~~~~~~~~~~~~~~~

:Description: Test signal frequency for AC impedance measurement.
:Type:        Integer
:Default:     200 (0xC8 = 18.75 kHz)
:Range:       0x01 to 0xFF (0x00 reserved)
:Formula:     Frequency = 93.75 Hz × register value
:Register:    0xB8

CHx AC LDG Real / CHx AC LDG Imag
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

:Description: Real and imaginary AC impedance components per channel
              (x = 1, 2, 3, 4).
:Type:        Integer (read-only)
:Range:       0x00 to 0xFF (8-bit signed)
:Register:    0xC3 (CH1 Real), 0xC4 (CH1 Imag), 0xC5 (CH2 Real), 0xC6 (CH2 Imag),
              0xC7 (CH3 Real), 0xC8 (CH3 Imag), 0xC9 (CH4 Real), 0xCA (CH4 Imag)
:Note:        Scale set by AC LDG Gain.

Speaker Protection & Detection
-------------------------------

Tweeter Detection Switch
~~~~~~~~~~~~~~~~~~~~~~~~

:Description: Enables tweeter detection using the AC impedance magnitude comparator.
:Type:        Boolean Switch
:Default:     Enabled (1)
:Register:    0xB6 bit [0]
:Note:        The underlying register bit is TWEETER DETECT DISABLE (active-low).
              Control value 1 = detection enabled (register bit 0), 0 = disabled
              (register bit 1).

Tweeter Detect Threshold
~~~~~~~~~~~~~~~~~~~~~~~~~

:Description: Magnitude threshold for tweeter detection.
:Type:        Integer
:Default:     0
:Range:       0x00 to 0xFF
:Resolution:  0.8 ohm/code (AC LDG Gain=0) or 0.1 ohm/code (AC LDG Gain=1)
:Register:    0xB7

CHx Tweeter Detect Report
~~~~~~~~~~~~~~~~~~~~~~~~~~

:Description: Tweeter detection result per channel (x = 1, 2, 3, 4).
:Type:        Boolean (read-only)
:Values:      0 = no tweeter detected, 1 = tweeter detected
:Register:    0xCB bit [3] (CH1), bit [2] (CH2), bit [1] (CH3), bit [0] (CH4)

DSP Protection Features
=======================

These controls are unavailable in LLP mode.

Thermal Foldback Switch
-----------------------

:Description: Enables dynamic gain reduction based on die temperature.
:Type:        Boolean Switch
:Default:     Disabled (0)
:Register:    0x3A bit [0]

PVDD Foldback Switch
--------------------

:Description: Enables automatic gain limiting when supply voltage drops
              (Automatic Gain Limiter).
:Type:        Boolean Switch
:Default:     Disabled (0)
:Register:    0x3A bit [4]

DC Blocker Bypass Switch
------------------------

:Description: Bypasses the DC-blocking high-pass filter.
:Type:        Boolean Switch
:Default:     Not bypassed (0)
:Register:    0x39 bit [0]

Clip Detect Switch
------------------

:Description: Enables DSP-based clip detection (Pseudo-Analog Clip Detect).
:Type:        Boolean Switch
:Default:     Disabled (0)
:Register:    0x93 bit [6]

Audio SDOUT Switch
------------------

:Description: Routes post-processed audio to the SDOUT pin instead of
              Vpredict data.
:Type:        Boolean Switch
:Default:     Disabled (0)
:Register:    0x3A bit [5]
:Note:        When enabled, replaces Vpredict data on SDOUT with post-processed
              SDIN. All SDOUT configurations that apply to Vpredict also apply
              to SDIN-to-SDOUT transmission.

Real-Time Load Diagnostics
===========================

These controls require Normal DSP mode at 48 kHz or 96 kHz. They are
unavailable at 192 kHz and in FFLP and LLP modes.


CHx RTLDG Switch
----------------

:Description: Enables real-time impedance monitoring during playback
              (x = 1, 2, 3, 4).
:Type:        Boolean Switch
:Default:     Disabled (0)
:Register:    0x37 bit [3] (CH1), bit [2] (CH2), bit [1] (CH3), bit [0] (CH4)

RTLDG Clip Mask Switch
----------------------

:Description: Suppresses impedance updates during clipping events.
:Type:        Boolean Switch
:Default:     Enabled (1)
:Register:    0x37 bit [4]

ISENSE Calibration Switch
--------------------------

:Description: Enables current sense calibration for accurate impedance
              measurements.
:Type:        Boolean Switch
:Default:     Disabled (0)
:Register:    0x5B bit [3]

RTLDG Open Load Threshold
--------------------------

:Description: DSP coefficient for open load fault detection threshold.
:Type:        DSP coefficient (extended control)
:Register:    DSP Book 0x8C, page 0x22, 0x98

RTLDG Short Load Threshold
---------------------------

:Description: DSP coefficient for shorted load fault detection threshold.
:Type:        DSP coefficient (extended control)
:Register:    DSP Book 0x8C, page 0x22, 0x9C

CHx RTLDG Impedance
-------------------

:Description: Real-time load impedance per channel (x = 1, 2, 3, 4).
:Type:        Float (read-only, displayed in ohms)
:Register:    0xD1–0xD2 (CH1), 0xD3–0xD4 (CH2), 0xD5–0xD6 (CH3), 0xD7–0xD8 (CH4)
:Note:        Valid only during PLAY state with RTLDG enabled at 48 or
              96 kHz. Holds stale data in SLEEP, MUTE, or Hi-Z states.

Fault Monitoring
================

The driver monitors faults via IRQ or periodic polling (fallback when
no ``interrupts`` DT property is present). Detected faults are logged
and cleared so affected channels can recover.

Kernel log messages use the format::

  tas675x <addr>: <Name> Latched: 0x<value>

Critical faults are logged at ``CRIT`` level; warnings at ``WARN``.

The following fault registers are monitored:

Critical Faults
---------------

These faults place affected channels into the FAULT state. The driver
issues fault clear (register 0x01 bit 3) to allow recovery.

Overtemperature Shutdown (0x87)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  ========  ==========================================
  Bits      Description
  ========  ==========================================
  [4]       Global OTSD
  [3:0]     CH1-CH4 OTSD respectively
  ========  ==========================================

Fires when die temperature exceeds the OTSD threshold. If auto-recovery
is enabled (``OTSD Auto Recovery Switch``), channels return to their
previous state after cooling. Otherwise fault clear is required.

Overcurrent / DC Fault (0x8E)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  ========  ==========================================
  Bits      Description
  ========  ==========================================
  [7:4]     Overcurrent Shutdown, CH1-CH4 respectively
  [3:0]     DC Fault, CH1-CH4 respectively
  ========  ==========================================

Overcurrent shutdown occurs when output current reaches the shutdown
threshold (e.g. output short to GND). DC fault fires when DC offset
exceeds the DCFAULT threshold.

Real-Time Load Diagnostic Fault (0x8B)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  ========  ==========================================
  Bits      Description
  ========  ==========================================
  [7:4]     Shorted Load faults, CH1-CH4 respectively
  [3:0]     Open Load faults, CH1-CH4 respectively
  ========  ==========================================

Reported during playback when RTLDG detects impedance outside the
configured open/short thresholds. Requires RTLDG and current sense to
be enabled, and a sampling rate of 48 kHz or 96 kHz.

CBC Fault/Warning (0x8D)
~~~~~~~~~~~~~~~~~~~~~~~~

  ========  ==========================================
  Bits      Description
  ========  ==========================================
  [7:4]     Load current warning, CH1-CH4 respectively
  [3:0]     Load current fault, CH1-CH4 respectively
  ========  ==========================================

Cycle-by-cycle current limiting events. A warning fires when the
current limit is active for at least 25% of a 21.3 ms window. A fault
fires if the warning persists for 170.4 ms continuously.

Warnings
--------

These conditions are logged but do not place channels into FAULT state.
Corresponding register bits are cleared when read. Nonetheless, the driver
still clears all events as warnings also latch the (mapped) FAULT pin.

Power Fault (0x86)
~~~~~~~~~~~~~~~~~~

  ========  ==========================================
  Bits      Description
  ========  ==========================================
  [7]       DVDD Power-On-Reset
  [4]       DVDD Undervoltage
  [3]       PVDD Overvoltage
  [2]       VBAT Overvoltage
  [1]       PVDD Undervoltage
  [0]       VBAT Undervoltage
  ========  ==========================================

Supply-related events. DVDD POR at bit 7 is expected after every
power-up and can be ignored.

CP / OUTM Soft Short Fault (0x7D)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  ========  ==========================================
  Bits      Description
  ========  ==========================================
  [5:2]     OUTM soft short, CH4-CH1 respectively
  [1]       Charge Pump UVLO (latched)
  [0]       Charge Pump UVLO (unlatched/live status)
  ========  ==========================================

Per-channel out_minus soft short faults and charge pump undervoltage lockout.

Clock Fault (0x8A)
~~~~~~~~~~~~~~~~~~

  ========  ==========================================
  Bits      Description
  ========  ==========================================
  [0]       Clock error
  ========  ==========================================

Fires on clock halt or unsupported SCLK/FSYNC ratio. Only logged when
a stream is active (suppressed during idle to avoid noise from normal
stream stop/start transitions). See `Clock Fault Behaviour`_ below.

Overtemperature Warning (0x88)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  ========  ==========================================
  Bits      Description
  ========  ==========================================
  [4]       Global OTW
  [3:0]     CH1-CH4 OTW respectively
  ========  ==========================================

Fires when temperature crosses the OTW threshold. The device continues
to operate. Only logged on change to avoid log spam.

Clip Warning (0x89)
~~~~~~~~~~~~~~~~~~~

  ========  ==========================================
  Bits      Description
  ========  ==========================================
  [3:0]     CH1-CH4 clip detected respectively
  ========  ==========================================

Fires when the audio signal magnitude exceeds the clip detection
threshold. Requires Clip Detect to be enabled. Only logged when a
stream is active.

Driver Known Limitations
========================

Clock Fault Behaviour
---------------------

On Stream Stop
~~~~~~~~~~~~~~

Every time a playback stream stops the FAULT pin briefly asserts. The
ASoC PCM trigger stop sequence calls DAIs in reverse order, so the CPU
DAI stops SCLK before the codec can transition to sleep. The device
detects the clock halt and latches ``CLK_FAULT_LATCHED``, which asserts
the FAULT pin. The driver clears the latch in the ``mute_stream``
callback that follows, so the FAULT pin flicker lasts only a few
milliseconds. Audio output is not affected and no kernel log message
is produced.

On Rapid Rate Switching
~~~~~~~~~~~~~~~~~~~~~~~

When streams are started in rapid succession, an intermittent
``Clock Fault Latched: 0x01`` message may appear in the kernel log.
A (conservative) ~0.5 second gap between sessions eliminates this.

References
==========

- TAS675x Technical Reference Manual: SLOU589A
- Device Tree Bindings: Documentation/devicetree/bindings/sound/ti,tas67524.yaml
- ALSA Control Name Conventions: Documentation/sound/designs/control-names.rst
