=====================
Exynos Emulation Mode
=====================

Copyright (C) 2012 Samsung Electronics

Written by Jonghwa Lee <jonghwa3.lee@samsung.com>

Description
-----------

Exynos 4x12 (4212, 4412) and 5 series provide emulation mode for thermal
management unit. Thermal emulation mode supports software debug for
TMU's operation. User can set temperature manually with software code
and TMU will read current temperature from user value not from sensor's
value.

Enabling CONFIG_THERMAL_EMULATION option will make this support
available. When it's enabled, sysfs node will be created as
/sys/devices/virtual/thermal/thermal_zone'zone id'/emul_temp.

The sysfs node, 'emul_node', will contain value 0 for the initial state.
When you input any temperature you want to update to sysfs node, it
automatically enable emulation mode and current temperature will be
changed into it.

(Exynos also supports user changeable delay time which would be used to
delay of changing temperature. However, this node only uses same delay
of real sensing time, 938us.)

Exynos emulation mode requires synchronous of value changing and
enabling. It means when you want to update the any value of delay or
next temperature, then you have to enable emulation mode at the same
time. (Or you have to keep the mode enabling.) If you don't, it fails to
change the value to updated one and just use last succeessful value
repeatedly. That's why this node gives users the right to change
termerpature only. Just one interface makes it more simply to use.

Disabling emulation mode only requires writing value 0 to sysfs node.

::


  TEMP	120 |
	    |
	100 |
	    |
	 80 |
	    |				 +-----------
	 60 |      			 |	    |
	    |		   +-------------|          |
	 40 |              |         	 |          |
	    |		   |		 |          |
	 20 |		   |		 |          +----------
	    |		   |		 |          |          |
	  0 |______________|_____________|__________|__________|_________
		   A		 A	    A		       A     TIME
		   |<----->|	 |<----->|  |<----->|	       |
		   | 938us |  	 |	 |  |       |          |
  emulation   : 0  50	   |  	 70      |  20      |          0
  current temp:   sensor   50		 70         20	      sensor
