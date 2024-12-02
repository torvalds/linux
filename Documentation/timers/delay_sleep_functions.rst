.. SPDX-License-Identifier: GPL-2.0

Delay and sleep mechanisms
==========================

This document seeks to answer the common question: "What is the
RightWay (TM) to insert a delay?"

This question is most often faced by driver writers who have to
deal with hardware delays and who may not be the most intimately
familiar with the inner workings of the Linux Kernel.

The following table gives a rough overview about the existing function
'families' and their limitations. This overview table does not replace the
reading of the function description before usage!

.. list-table::
   :widths: 20 20 20 20 20
   :header-rows: 2

   * -
     - `*delay()`
     - `usleep_range*()`
     - `*sleep()`
     - `fsleep()`
   * -
     - busy-wait loop
     - hrtimers based
     - timer list timers based
     - combines the others
   * - Usage in atomic Context
     - yes
     - no
     - no
     - no
   * - precise on "short intervals"
     - yes
     - yes
     - depends
     - yes
   * - precise on "long intervals"
     - Do not use!
     - yes
     - max 12.5% slack
     - yes
   * - interruptible variant
     - no
     - yes
     - yes
     - no

A generic advice for non atomic contexts could be:

#. Use `fsleep()` whenever unsure (as it combines all the advantages of the
   others)
#. Use `*sleep()` whenever possible
#. Use `usleep_range*()` whenever accuracy of `*sleep()` is not sufficient
#. Use `*delay()` for very, very short delays

Find some more detailed information about the function 'families' in the next
sections.

`*delay()` family of functions
------------------------------

These functions use the jiffy estimation of clock speed and will busy wait for
enough loop cycles to achieve the desired delay. udelay() is the basic
implementation and ndelay() as well as mdelay() are variants.

These functions are mainly used to add a delay in atomic context. Please make
sure to ask yourself before adding a delay in atomic context: Is this really
required?

.. kernel-doc:: include/asm-generic/delay.h
	:identifiers: udelay ndelay

.. kernel-doc:: include/linux/delay.h
	:identifiers: mdelay


`usleep_range*()` and `*sleep()` family of functions
----------------------------------------------------

These functions use hrtimers or timer list timers to provide the requested
sleeping duration. In order to decide which function is the right one to use,
take some basic information into account:

#. hrtimers are more expensive as they are using an rb-tree (instead of hashing)
#. hrtimers are more expensive when the requested sleeping duration is the first
   timer which means real hardware has to be programmed
#. timer list timers always provide some sort of slack as they are jiffy based

The generic advice is repeated here:

#. Use `fsleep()` whenever unsure (as it combines all the advantages of the
   others)
#. Use `*sleep()` whenever possible
#. Use `usleep_range*()` whenever accuracy of `*sleep()` is not sufficient

First check fsleep() function description and to learn more about accuracy,
please check msleep() function description.


`usleep_range*()`
~~~~~~~~~~~~~~~~~

.. kernel-doc:: include/linux/delay.h
	:identifiers: usleep_range usleep_range_idle

.. kernel-doc:: kernel/time/sleep_timeout.c
	:identifiers: usleep_range_state


`*sleep()`
~~~~~~~~~~

.. kernel-doc:: kernel/time/sleep_timeout.c
       :identifiers: msleep msleep_interruptible

.. kernel-doc:: include/linux/delay.h
	:identifiers: ssleep fsleep
