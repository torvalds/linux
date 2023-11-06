.. SPDX-License-Identifier: GPL-2.0

The Virtual PCM Test Driver
===========================

The Virtual PCM Test Driver emulates a generic PCM device, and can be used for
testing/fuzzing of the userspace ALSA applications, as well as for testing/fuzzing of
the PCM middle layer. Additionally, it can be used for simulating hard to reproduce
problems with PCM devices.

What can this driver do?
~~~~~~~~~~~~~~~~~~~~~~~~

At this moment the driver can do the following things:
	* Simulate both capture and playback processes
	* Generate random or pattern-based capturing data
	* Inject delays into the playback and capturing processes
	* Inject errors during the PCM callbacks

It supports up to 8 substreams and 4 channels. Also it supports both interleaved and
non-interleaved access modes.

Also, this driver can check the playback stream for containing the predefined pattern,
which is used in the corresponding selftest (alsa/pcmtest-test.sh) to check the PCM middle
layer data transferring functionality. Additionally, this driver redefines the default
RESET ioctl, and the selftest covers this PCM API functionality as well.

Configuration
-------------

The driver has several parameters besides the common ALSA module parameters:

	* fill_mode (bool) - Buffer fill mode (see below)
	* inject_delay (int)
	* inject_hwpars_err (bool)
	* inject_prepare_err (bool)
	* inject_trigger_err (bool)


Capture Data Generation
-----------------------

The driver has two modes of data generation: the first (0 in the fill_mode parameter)
means random data generation, the second (1 in the fill_mode) - pattern-based
data generation. Let's look at the second mode.

First of all, you may want to specify the pattern for data generation. You can do it
by writing the pattern to the debugfs file. There are pattern buffer debugfs entries
for each channel, as well as entries which contain the pattern buffer length.

	* /sys/kernel/debug/pcmtest/fill_pattern[0-3]
	* /sys/kernel/debug/pcmtest/fill_pattern[0-3]_len

To set the pattern for the channel 0 you can execute the following command:

.. code-block:: bash

	echo -n mycoolpattern > /sys/kernel/debug/pcmtest/fill_pattern0

Then, after every capture action performed on the 'pcmtest' device the buffer for the
channel 0 will contain 'mycoolpatternmycoolpatternmycoolpatternmy...'.

The pattern itself can be up to 4096 bytes long.

Delay injection
---------------

The driver has 'inject_delay' parameter, which has very self-descriptive name and
can be used for time delay/speedup simulations. The parameter has integer type, and
it means the delay added between module's internal timer ticks.

If the 'inject_delay' value is positive, the buffer will be filled slower, if it is
negative - faster. You can try it yourself by starting a recording in any
audiorecording application (like Audacity) and selecting the 'pcmtest' device as a
source.

This parameter can be also used for generating a huge amount of sound data in a very
short period of time (with the negative 'inject_delay' value).

Errors injection
----------------

This module can be used for injecting errors into the PCM communication process. This
action can help you to figure out how the userspace ALSA program behaves under unusual
circumstances.

For example, you can make all 'hw_params' PCM callback calls return EBUSY error by
writing '1' to the 'inject_hwpars_err' module parameter:

.. code-block:: bash

	echo 1 > /sys/module/snd_pcmtest/parameters/inject_hwpars_err

Errors can be injected into the following PCM callbacks:

	* hw_params (EBUSY)
	* prepare (EINVAL)
	* trigger (EINVAL)

Playback test
-------------

This driver can be also used for the playback functionality testing - every time you
write the playback data to the 'pcmtest' PCM device and close it, the driver checks the
buffer for containing the looped pattern (which is specified in the fill_pattern
debugfs file for each channel). If the playback buffer content represents the looped
pattern, 'pc_test' debugfs entry is set into '1'. Otherwise, the driver sets it to '0'.

ioctl redefinition test
-----------------------

The driver redefines the 'reset' ioctl, which is default for all PCM devices. To test
this functionality, we can trigger the reset ioctl and check the 'ioctl_test' debugfs
entry:

.. code-block:: bash

	cat /sys/kernel/debug/pcmtest/ioctl_test

If the ioctl is triggered successfully, this file will contain '1', and '0' otherwise.
