=====================
Kernel driver asc7621
=====================

Supported chips:

    Andigilog aSC7621 and aSC7621a

    Prefix: 'asc7621'

    Addresses scanned: I2C 0x2c, 0x2d, 0x2e

    Datasheet: http://www.fairview5.com/linux/asc7621/asc7621.pdf

Author:
		George Joseph

Description provided by Dave Pivin @ Andigilog:

Andigilog has both the PECI and pre-PECI versions of the Heceta-6, as
Intel calls them. Heceta-6e has high frequency PWM and Heceta-6p has
added PECI and a 4th thermal zone. The Andigilog aSC7611 is the
Heceta-6e part and aSC7621 is the Heceta-6p part. They are both in
volume production, shipping to Intel and their subs.

We have enhanced both parts relative to the governing Intel
specification. First enhancement is temperature reading resolution. We
have used registers below 20h for vendor-specific functions in addition
to those in the Intel-specified vendor range.

Our conversion process produces a result that is reported as two bytes.
The fan speed control uses this finer value to produce a "step-less" fan
PWM output. These two bytes are "read-locked" to guarantee that once a
high or low byte is read, the other byte is locked-in until after the
next read of any register. So to get an atomic reading, read high or low
byte, then the very next read should be the opposite byte. Our data
sheet says 10-bits of resolution, although you may find the lower bits
are active, they are not necessarily reliable or useful externally. We
chose not to mask them.

We employ significant filtering that is user tunable as described in the
data sheet. Our temperature reports and fan PWM outputs are very smooth
when compared to the competition, in addition to the higher resolution
temperature reports. The smoother PWM output does not require user
intervention.

We offer GPIO features on the former VID pins. These are open-drain
outputs or inputs and may be used as general purpose I/O or as alarm
outputs that are based on temperature limits. These are in 19h and 1Ah.

We offer flexible mapping of temperature readings to thermal zones. Any
temperature may be mapped to any zone, which has a default assignment
that follows Intel's specs.

Since there is a fan to zone assignment that allows for the "hotter" of
a set of zones to control the PWM of an individual fan, but there is no
indication to the user, we have added an indicator that shows which zone
is currently controlling the PWM for a given fan. This is in register
00h.

Both remote diode temperature readings may be given an offset value such
that the reported reading as well as the temperature used to determine
PWM may be offset for system calibration purposes.

PECI Extended configuration allows for having more than two domains per
PECI address and also provides an enabling function for each PECI
address. One could use our flexible zone assignment to have a zone
assigned to up to 4 PECI addresses. This is not possible in the default
Intel configuration. This would be useful in multi-CPU systems with
individual fans on each that would benefit from individual fan control.
This is in register 0Eh.

The tachometer measurement system is flexible and able to adapt to many
fan types. We can also support pulse-stretched PWM so that 3-wire fans
may be used. These characteristics are in registers 04h to 07h.

Finally, we have added a tach disable function that turns off the tach
measurement system for individual tachs in order to save power. That is
in register 75h.

--------------------------------------------------------------------------

aSC7621 Product Description
===========================

The aSC7621 has a two wire digital interface compatible with SMBus 2.0.
Using a 10-bit ADC, the aSC7621 measures the temperature of two remote diode
connected transistors as well as its own die. Support for Platform
Environmental Control Interface (PECI) is included.

Using temperature information from these four zones, an automatic fan speed
control algorithm is employed to minimize acoustic impact while achieving
recommended CPU temperature under varying operational loads.

To set fan speed, the aSC7621 has three independent pulse width modulation
(PWM) outputs that are controlled by one, or a combination of three,
temperature zones. Both high- and low-frequency PWM ranges are supported.

The aSC7621 also includes a digital filter that can be invoked to smooth
temperature readings for better control of fan speed and minimum acoustic
impact.

The aSC7621 has tachometer inputs to measure fan speed on up to four fans.
Limit and status registers for all measured values are included to alert
the system host that any measurements are outside of programmed limits
via status registers.

System voltages of VCCP, 2.5V, 3.3V, 5.0V, and 12V motherboard power are
monitored efficiently with internal scaling resistors.

Features
--------

- Supports PECI interface and monitors internal and remote thermal diodes
- 2-wire, SMBus 2.0 compliant, serial interface
- 10-bit ADC
- Monitors VCCP, 2.5V, 3.3V, 5.0V, and 12V motherboard/processor supplies
- Programmable autonomous fan control based on temperature readings
- Noise filtering of temperature reading for fan speed control
- 0.25C digital temperature sensor resolution
- 3 PWM fan speed control outputs for 2-, 3- or 4-wire fans and up to 4 fan
  tachometer inputs
- Enhanced measured temperature to Temperature Zone assignment.
- Provides high and low PWM frequency ranges
- 3 GPIO pins for custom use
- 24-Lead QSOP package

Configuration Notes
===================

Except where noted below, the sysfs entries created by this driver follow
the standards defined in "sysfs-interface".

temp1_source
	=	===============================================
	0 	(default) peci_legacy = 0, Remote 1 Temperature
		peci_legacy = 1, PECI Processor Temperature 0
	1 	Remote 1 Temperature
	2 	Remote 2 Temperature
	3 	Internal Temperature
	4 	PECI Processor Temperature 0
	5 	PECI Processor Temperature 1
	6 	PECI Processor Temperature 2
	7	PECI Processor Temperature 3
	=	===============================================

temp2_source
	=	===============================================
	0 	(default) Internal Temperature
	1 	Remote 1 Temperature
	2 	Remote 2 Temperature
	3 	Internal Temperature
	4 	PECI Processor Temperature 0
	5 	PECI Processor Temperature 1
	6 	PECI Processor Temperature 2
	7 	PECI Processor Temperature 3
	=	===============================================

temp3_source
	=	===============================================
	0 	(default) Remote 2 Temperature
	1 	Remote 1 Temperature
	2 	Remote 2 Temperature
	3 	Internal Temperature
	4 	PECI Processor Temperature 0
	5 	PECI Processor Temperature 1
	6 	PECI Processor Temperature 2
	7 	PECI Processor Temperature 3
	=	===============================================

temp4_source
	=	===============================================
	0 	(default) peci_legacy = 0, PECI Processor Temperature 0
		peci_legacy = 1, Remote 1 Temperature
	1 	Remote 1 Temperature
	2 	Remote 2 Temperature
	3 	Internal Temperature
	4 	PECI Processor Temperature 0
	5 	PECI Processor Temperature 1
	6 	PECI Processor Temperature 2
	7 	PECI Processor Temperature 3
	=	===============================================

temp[1-4]_smoothing_enable / temp[1-4]_smoothing_time
	Smooths spikes in temp readings caused by noise.
	Valid values in milliseconds are:

	* 35000
	* 17600
	* 11800
	*  7000
	*  4400
	*  3000
	*  1600
	*   800

temp[1-4]_crit
	When the corresponding zone temperature reaches this value,
	ALL pwm outputs will got to 100%.

temp[5-8]_input / temp[5-8]_enable
	The aSC7621 can also read temperatures provided by the processor
	via the PECI bus.  Usually these are "core" temps and are relative
	to the point where the automatic thermal control circuit starts
	throttling.  This means that these are usually negative numbers.

pwm[1-3]_enable
	=============== ========================================================
	0		Fan off.
	1		Fan on manual control.
	2		Fan on automatic control and will run at the minimum pwm
			if the temperature for the zone is below the minimum.
	3		Fan on automatic control but will be off if the
			temperature for the zone is below the minimum.
	4-254		Ignored.
	255		Fan on full.
	=============== ========================================================

pwm[1-3]_auto_channels
	Bitmap as described in sysctl-interface with the following
	exceptions...

	Only the following combination of zones (and their corresponding masks)
	are valid:

	* 1
	* 2
	* 3
	* 2,3
	* 1,2,3
	* 4
	* 1,2,3,4

	* Special values:

	  ==		======================
	  0		Disabled.
	  16		Fan on manual control.
	  31		Fan on full.
	  ==		======================


pwm[1-3]_invert
	When set, inverts the meaning of pwm[1-3].
	i.e.  when pwm = 0, the fan will be on full and
	when pwm = 255 the fan will be off.

pwm[1-3]_freq
	PWM frequency in Hz
	Valid values in Hz are:

	* 10
	* 15
	* 23
	* 30  (default)
	* 38
	* 47
	* 62
	* 94
	* 23000
	* 24000
	* 25000
	* 26000
	* 27000
	* 28000
	* 29000
	* 30000

	Setting any other value will be ignored.

peci_enable
	Enables or disables PECI

peci_avg
	Input filter average time.

	* 0 	0 Sec. (no Smoothing) (default)
	* 1 	0.25 Sec.
	* 2 	0.5 Sec.
	* 3 	1.0 Sec.
	* 4 	2.0 Sec.
	* 5 	4.0 Sec.
	* 6 	8.0 Sec.
	* 7 	0.0 Sec.

peci_legacy
	=	============================================
	0	Standard Mode (default)
		Remote Diode 1 reading is associated with
		Temperature Zone 1, PECI is associated with
		Zone 4

	1	Legacy Mode
		PECI is associated with Temperature Zone 1,
		Remote Diode 1 is associated with Zone 4
	=	============================================

peci_diode
	Diode filter

	=	====================
	0	0.25 Sec.
	1 	1.1 Sec.
	2 	2.4 Sec.  (default)
	3 	3.4 Sec.
	4 	5.0 Sec.
	5 	6.8 Sec.
	6 	10.2 Sec.
	7 	16.4 Sec.
	=	====================

peci_4domain
	Four domain enable

	=	===============================================
	0 	1 or 2 Domains for enabled processors (default)
	1 	3 or 4 Domains for enabled processors
	=	===============================================

peci_domain
	Domain

	=	==================================================
	0 	Processor contains a single domain (0) 	 (default)
	1 	Processor contains two domains (0,1)
	=	==================================================
