Kernel driver coretemp
======================

Supported chips:
  * All Intel Core family

    Prefix: 'coretemp'

    CPUID: family 0x6, models

			    - 0xe (Pentium M DC), 0xf (Core 2 DC 65nm),
			    - 0x16 (Core 2 SC 65nm), 0x17 (Penryn 45nm),
			    - 0x1a (Nehalem), 0x1c (Atom), 0x1e (Lynnfield),
			    - 0x26 (Tunnel Creek Atom), 0x27 (Medfield Atom),
			    - 0x36 (Cedar Trail Atom)

    Datasheet:

	       Intel 64 and IA-32 Architectures Software Developer's Manual
	       Volume 3A: System Programming Guide

	       http://softwarecommunity.intel.com/Wiki/Mobility/720.htm

Author: Rudolf Marek

Description
-----------

This driver permits reading the DTS (Digital Temperature Sensor) embedded
inside Intel CPUs. This driver can read both the per-core and per-package
temperature using the appropriate sensors. The per-package sensor is new;
as of now, it is present only in the SandyBridge platform. The driver will
show the temperature of all cores inside a package under a single device
directory inside hwmon.

Temperature is measured in degrees Celsius and measurement resolution is
1 degree C. Valid temperatures are from 0 to TjMax degrees C, because
the actual value of temperature register is in fact a delta from TjMax.

Temperature known as TjMax is the maximum junction temperature of processor,
which depends on the CPU model. See table below. At this temperature, protection
mechanism will perform actions to forcibly cool down the processor. Alarm
may be raised, if the temperature grows enough (more than TjMax) to trigger
the Out-Of-Spec bit. Following table summarizes the exported sysfs files:

All Sysfs entries are named with their core_id (represented here by 'X').

================= ========================================================
tempX_input	  Core temperature (in millidegrees Celsius).
tempX_max	  All cooling devices should be turned on (on Core2).
tempX_crit	  Maximum junction temperature (in millidegrees Celsius).
tempX_crit_alarm  Set when Out-of-spec bit is set, never clears.
		  Correct CPU operation is no longer guaranteed.
tempX_label	  Contains string "Core X", where X is processor
		  number. For Package temp, this will be "Physical id Y",
		  where Y is the package number.
================= ========================================================

On CPU models which support it, TjMax is read from a model-specific register.
On other models, it is set to an arbitrary value based on weak heuristics.
If these heuristics don't work for you, you can pass the correct TjMax value
as a module parameter (tjmax).

Appendix A. Known TjMax lists (TBD):
Some information comes from ark.intel.com

=============== =============================================== ================
Process		Processor					TjMax(C)

22nm		Core i5/i7 Processors
		i7 3920XM, 3820QM, 3720QM, 3667U, 3520M		105
		i5 3427U, 3360M/3320M				105
		i7 3770/3770K					105
		i5 3570/3570K, 3550, 3470/3450			105
		i7 3770S					103
		i5 3570S/3550S, 3475S/3470S/3450S		103
		i7 3770T					94
		i5 3570T					94
		i5 3470T					91

32nm		Core i3/i5/i7 Processors
		i7 2600						98
		i7 660UM/640/620, 640LM/620, 620M, 610E		105
		i5 540UM/520/430, 540M/520/450/430		105
		i3 330E, 370M/350/330				90 rPGA, 105 BGA
		i3 330UM					105

32nm		Core i7 Extreme Processors
		980X						100

32nm		Celeron Processors
		U3400						105
		P4505/P4500 					90

32nm		Atom Processors
		S1260/1220					95
		S1240						102
		Z2460						90
		Z2760						90
		D2700/2550/2500					100
		N2850/2800/2650/2600				100

45nm		Xeon Processors 5400 Quad-Core
		X5492, X5482, X5472, X5470, X5460, X5450	85
		E5472, E5462, E5450/40/30/20/10/05		85
		L5408						95
		L5430, L5420, L5410				70

45nm		Xeon Processors 5200 Dual-Core
		X5282, X5272, X5270, X5260			90
		E5240						90
		E5205, E5220					70, 90
		L5240						70
		L5238, L5215					95

45nm		Atom Processors
		D525/510/425/410				100
		K525/510/425/410				100
		Z670/650					90
		Z560/550/540/530P/530/520PT/520/515/510PT/510P	90
		Z510/500					90
		N570/550					100
		N475/470/455/450				100
		N280/270					90
		330/230						125
		E680/660/640/620				90
		E680T/660T/640T/620T				110
		E665C/645C					90
		E665CT/645CT					110
		CE4170/4150/4110				110
		CE4200 series					unknown
		CE5300 series					unknown

45nm		Core2 Processors
		Solo ULV SU3500/3300				100
		T9900/9800/9600/9550/9500/9400/9300/8300/8100	105
		T6670/6500/6400					105
		T6600						90
		SU9600/9400/9300				105
		SP9600/9400					105
		SL9600/9400/9380/9300				105
		P9700/9600/9500/8800/8700/8600/8400/7570	105
		P7550/7450					90

45nm		Core2 Quad Processors
		Q9100/9000					100

45nm		Core2 Extreme Processors
		X9100/9000					105
		QX9300						100

45nm		Core i3/i5/i7 Processors
		i7 940XM/920					100
		i7 840QM/820/740/720				100

45nm		Celeron Processors
		SU2300						100
		900 						105

65nm		Core2 Duo Processors
		Solo U2200, U2100				100
		U7700/7600/7500					100
		T7800/7700/7600/7500/7400/7300/7250/7200/7100	100
		T5870/5670/5600/5550/5500/5470/5450/5300/5270	100
		T5250						100
		T5800/5750/5200					85
		L7700/7500/7400/7300/7200			100

65nm		Core2 Extreme Processors
		X7900/7800					100

65nm		Core Duo Processors
		U2500/2400					100
		T2700/2600/2450/2400/2350/2300E/2300/2250/2050	100
		L2500/2400/2300					100

65nm		Core Solo Processors
		U1500/1400/1300					100
		T1400/1350/1300/1250				100

65nm		Xeon Processors 5000 Quad-Core
		X5000						90-95
		E5000						80
		L5000						70
		L5318						95

65nm		Xeon Processors 5000 Dual-Core
		5080, 5063, 5060, 5050, 5030			80-90
		5160, 5150, 5148, 5140, 5130, 5120, 5110	80
		L5138						100

65nm		Celeron Processors
		T1700/1600					100
		560/550/540/530					100
=============== =============================================== ================
