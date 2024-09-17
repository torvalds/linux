.. SPDX-License-Identifier: GPL-2.0-only

Kernel driver bt1-pvt
=====================

Supported chips:

  * Baikal-T1 PVT sensor (in SoC)

    Prefix: 'bt1-pvt'

    Addresses scanned: -

    Datasheet: Provided by BAIKAL ELECTRONICS upon request and under NDA

Authors:
    Maxim Kaurkin <maxim.kaurkin@baikalelectronics.ru>
    Serge Semin <Sergey.Semin@baikalelectronics.ru>

Description
-----------

This driver implements support for the hardware monitoring capabilities of the
embedded into Baikal-T1 process, voltage and temperature sensors. PVT IP-core
consists of one temperature and four voltage sensors, which can be used to
monitor the chip internal environment like heating, supply voltage and
transistors performance. The driver can optionally provide the hwmon alarms
for each sensor the PVT controller supports. The alarms functionality is made
compile-time configurable due to the hardware interface implementation
peculiarity, which is connected with an ability to convert data from only one
sensor at a time. Additional limitation is that the controller performs the
thresholds checking synchronously with the data conversion procedure. Due to
these in order to have the hwmon alarms automatically detected the driver code
must switch from one sensor to another, read converted data and manually check
the threshold status bits. Depending on the measurements timeout settings
(update_interval sysfs node value) this design may cause additional burden on
the system performance. So in case if alarms are unnecessary in your system
design it's recommended to have them disabled to prevent the PVT IRQs being
periodically raised to get the data cache/alarms status up to date. By default
in alarm-less configuration the data conversion is performed by the driver
on demand when read operation is requested via corresponding _input-file.

Temperature Monitoring
----------------------

Temperature is measured with 10-bit resolution and reported in millidegree
Celsius. The driver performs all the scaling by itself therefore reports true
temperatures that don't need any user-space adjustments. While the data
translation formulae isn't linear, which gives us non-linear discreteness,
it's close to one, but giving a bit better accuracy for higher temperatures.
The temperature input is mapped as follows (the last column indicates the input
ranges)::

	temp1: CPU embedded diode	-48.38C - +147.438C

In case if the alarms kernel config is enabled in the driver the temperature input
has associated min and max limits which trigger an alarm when crossed.

Voltage Monitoring
------------------

The voltage inputs are also sampled with 10-bit resolution and reported in
millivolts. But in this case the data translation formulae is linear, which
provides a constant measurements discreteness. The data scaling is also
performed by the driver, so returning true millivolts. The voltage inputs are
mapped as follows (the last column indicates the input ranges)::

	in0: VDD		(processor core)		0.62V - 1.168V
	in1: Low-Vt		(low voltage threshold)		0.62V - 1.168V
	in2: High-Vt		(high voltage threshold)	0.62V - 1.168V
	in3: Standard-Vt	(standard voltage threshold)	0.62V - 1.168V

In case if the alarms config is enabled in the driver the voltage inputs
have associated min and max limits which trigger an alarm when crossed.

Sysfs Attributes
----------------

Following is a list of all sysfs attributes that the driver provides, their
permissions and a short description:

=============================== ======= =======================================
Name				Perm	Description
=============================== ======= =======================================
update_interval			RW	Measurements update interval per
					sensor.
temp1_type			RO	Sensor type (always 1 as CPU embedded
					diode).
temp1_label			RO	CPU Core Temperature sensor.
temp1_input			RO	Measured temperature in millidegree
					Celsius.
temp1_min			RW	Low limit for temp input.
temp1_max			RW	High limit for temp input.
temp1_min_alarm			RO	Temperature input alarm. Returns 1 if
					temperature input went below min limit,
					0 otherwise.
temp1_max_alarm			RO	Temperature input alarm. Returns 1 if
					temperature input went above max limit,
					0 otherwise.
temp1_offset			RW	Temperature offset in millidegree
					Celsius which is added to the
					temperature reading by the chip. It can
					be used to manually adjust the
					temperature measurements within 7.130
					degrees Celsius.
in[0-3]_label			RO	CPU Voltage sensor (either core or
					low/high/standard thresholds).
in[0-3]_input			RO	Measured voltage in millivolts.
in[0-3]_min			RW	Low limit for voltage input.
in[0-3]_max			RW	High limit for voltage input.
in[0-3]_min_alarm		RO	Voltage input alarm. Returns 1 if
					voltage input went below min limit,
					0 otherwise.
in[0-3]_max_alarm		RO	Voltage input alarm. Returns 1 if
					voltage input went above max limit,
					0 otherwise.
=============================== ======= =======================================
