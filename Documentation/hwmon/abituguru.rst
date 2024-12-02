Kernel driver abituguru
=======================

Supported chips:

  * Abit uGuru revision 1 & 2 (Hardware Monitor part only)

    Prefix: 'abituguru'

    Addresses scanned: ISA 0x0E0

    Datasheet: Not available, this driver is based on reverse engineering.
    A "Datasheet" has been written based on the reverse engineering it
    should be available in the same dir as this file under the name
    abituguru-datasheet.

    Note:
	The uGuru is a microcontroller with onboard firmware which programs
	it to behave as a hwmon IC. There are many different revisions of the
	firmware and thus effectively many different revisions of the uGuru.
	Below is an incomplete list with which revisions are used for which
	Motherboards:

	- uGuru 1.00    ~ 1.24    (AI7, KV8-MAX3, AN7) [1]_
	- uGuru 2.0.0.0 ~ 2.0.4.2 (KV8-PRO)
	- uGuru 2.1.0.0 ~ 2.1.2.8 (AS8, AV8, AA8, AG8, AA8XE, AX8)
	- uGuru 2.2.0.0 ~ 2.2.0.6 (AA8 Fatal1ty)
	- uGuru 2.3.0.0 ~ 2.3.0.9 (AN8)
	- uGuru 3.0.0.0 ~ 3.0.x.x (AW8, AL8, AT8, NI8 SLI, AT8 32X, AN8 32X,
	  AW9D-MAX) [2]_

.. [1]  For revisions 2 and 3 uGuru's the driver can autodetect the
	sensortype (Volt or Temp) for bank1 sensors, for revision 1 uGuru's
	this does not always work. For these uGuru's the autodetection can
	be overridden with the bank1_types module param. For all 3 known
	revision 1 motherboards the correct use of this param is:
	bank1_types=1,1,0,0,0,0,0,2,0,0,0,0,2,0,0,1
	You may also need to specify the fan_sensors option for these boards
	fan_sensors=5

.. [2]  There is a separate abituguru3 driver for these motherboards,
	the abituguru (without the 3 !) driver will not work on these
	motherboards (and visa versa)!

Authors:
	- Hans de Goede <j.w.r.degoede@hhs.nl>,
	- (Initial reverse engineering done by Olle Sandberg
	  <ollebull@gmail.com>)


Module Parameters
-----------------

* force: bool
			Force detection. Note this parameter only causes the
			detection to be skipped, and thus the insmod to
			succeed. If the uGuru can't be read the actual hwmon
			driver will not load and thus no hwmon device will get
			registered.
* bank1_types: int[]
			Bank1 sensortype autodetection override:

			  * -1 autodetect (default)
			  *  0 volt sensor
			  *  1 temp sensor
			  *  2 not connected
* fan_sensors: int
			Tell the driver how many fan speed sensors there are
			on your motherboard. Default: 0 (autodetect).
* pwms: int
			Tell the driver how many fan speed controls (fan
			pwms) your motherboard has. Default: 0 (autodetect).
* verbose: int
			How verbose should the driver be? (0-3):

			   * 0 normal output
			   * 1 + verbose error reporting
			   * 2 + sensors type probing info (default)
			   * 3 + retryable error reporting

			Default: 2 (the driver is still in the testing phase)

Notice: if you need any of the first three options above please insmod the
driver with verbose set to 3 and mail me <j.w.r.degoede@hhs.nl> the output of:
dmesg | grep abituguru


Description
-----------

This driver supports the hardware monitoring features of the first and
second revision of the Abit uGuru chip found on Abit uGuru featuring
motherboards (most modern Abit motherboards).

The first and second revision of the uGuru chip in reality is a Winbond
W83L950D in disguise (despite Abit claiming it is "a new microprocessor
designed by the ABIT Engineers"). Unfortunately this doesn't help since the
W83L950D is a generic microcontroller with a custom Abit application running
on it.

Despite Abit not releasing any information regarding the uGuru, Olle
Sandberg <ollebull@gmail.com> has managed to reverse engineer the sensor part
of the uGuru. Without his work this driver would not have been possible.

Known Issues
------------

The voltage and frequency control parts of the Abit uGuru are not supported.

.. toctree::
   :maxdepth: 1

   abituguru-datasheet.rst
