=======================
S3C24XX CPUfreq support
=======================

Introduction
------------

 The S3C24XX series support a number of power saving systems, such as
 the ability to change the core, memory and peripheral operating
 frequencies. The core control is exported via the CPUFreq driver
 which has a number of different manual or automatic controls over the
 rate the core is running at.

 There are two forms of the driver depending on the specific CPU and
 how the clocks are arranged. The first implementation used as single
 PLL to feed the ARM, memory and peripherals via a series of dividers
 and muxes and this is the implementation that is documented here. A
 newer version where there is a separate PLL and clock divider for the
 ARM core is available as a separate driver.


Layout
------

 The code core manages the CPU specific drivers, any data that they
 need to register and the interface to the generic drivers/cpufreq
 system. Each CPU registers a driver to control the PLL, clock dividers
 and anything else associated with it. Any board that wants to use this
 framework needs to supply at least basic details of what is required.

 The core registers with drivers/cpufreq at init time if all the data
 necessary has been supplied.


CPU support
-----------

 The support for each CPU depends on the facilities provided by the
 SoC and the driver as each device has different PLL and clock chains
 associated with it.


Slow Mode
---------

 The SLOW mode where the PLL is turned off altogether and the
 system is fed by the external crystal input is currently not
 supported.


sysfs
-----

 The core code exports extra information via sysfs in the directory
 devices/system/cpu/cpu0/arch-freq.


Board Support
-------------

 Each board that wants to use the cpufreq code must register some basic
 information with the core driver to provide information about what the
 board requires and any restrictions being placed on it.

 The board needs to supply information about whether it needs the IO bank
 timings changing, any maximum frequency limits and information about the
 SDRAM refresh rate.




Document Author
---------------

Ben Dooks, Copyright 2009 Simtec Electronics
Licensed under GPLv2
