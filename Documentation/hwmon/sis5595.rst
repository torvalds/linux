Kernel driver sis5595
=====================

Supported chips:

  * Silicon Integrated Systems Corp. SiS5595 Southbridge Hardware Monitor

    Prefix: 'sis5595'

    Addresses scanned: ISA in PCI-space encoded address

    Datasheet: Publicly available at the Silicon Integrated Systems Corp. site.



Authors:

      - Kyösti Mälkki <kmalkki@cc.hut.fi>,
      - Mark D. Studebaker <mdsxyz123@yahoo.com>,
      - Aurelien Jarno <aurelien@aurel32.net> 2.6 port

   SiS southbridge has a LM78-like chip integrated on the same IC.
   This driver is a customized copy of lm78.c

   Supports following revisions:

       =============== =============== ==============
       Version         PCI ID          PCI Revision
       =============== =============== ==============
       1               1039/0008       AF or less
       2               1039/0008       B0 or greater
       =============== =============== ==============

   Note: these chips contain a 0008 device which is incompatible with the
	5595. We recognize these by the presence of the listed
	"blacklist" PCI ID and refuse to load.

   =================== =============== ================
   NOT SUPPORTED       PCI ID          BLACKLIST PCI ID
   =================== =============== ================
	540            0008            0540
	550            0008            0550
       5513            0008            5511
       5581            0008            5597
       5582            0008            5597
       5597            0008            5597
	630            0008            0630
	645            0008            0645
	730            0008            0730
	735            0008            0735
   =================== =============== ================


Module Parameters
-----------------

======================= =====================================================
force_addr=0xaddr	Set the I/O base address. Useful for boards
			that don't set the address in the BIOS. Does not do a
			PCI force; the device must still be present in lspci.
			Don't use this unless the driver complains that the
			base address is not set.

			Example: 'modprobe sis5595 force_addr=0x290'
======================= =====================================================


Description
-----------

The SiS5595 southbridge has integrated hardware monitor functions. It also
has an I2C bus, but this driver only supports the hardware monitor. For the
I2C bus driver see i2c-sis5595.

The SiS5595 implements zero or one temperature sensor, two fan speed
sensors, four or five voltage sensors, and alarms.

On the first version of the chip, there are four voltage sensors and one
temperature sensor.

On the second version of the chip, the temperature sensor (temp) and the
fifth voltage sensor (in4) share a pin which is configurable, but not
through the driver. Sorry. The driver senses the configuration of the pin,
which was hopefully set by the BIOS.

Temperatures are measured in degrees Celsius. An alarm is triggered once
when the max is crossed; it is also triggered when it drops below the min
value. Measurements are guaranteed between -55 and +125 degrees, with a
resolution of 1 degree.

Fan rotation speeds are reported in RPM (rotations per minute). An alarm is
triggered if the rotation speed has dropped below a programmable limit. Fan
readings can be divided by a programmable divider (1, 2, 4 or 8) to give
the readings more range or accuracy. Not all RPM values can accurately be
represented, so some rounding is done. With a divider of 2, the lowest
representable value is around 2600 RPM.

Voltage sensors (also known as IN sensors) report their values in volts. An
alarm is triggered if the voltage has crossed a programmable minimum or
maximum limit. Note that minimum in this case always means 'closest to
zero'; this is important for negative voltage measurements. All voltage
inputs can measure voltages between 0 and 4.08 volts, with a resolution of
0.016 volt.

In addition to the alarms described above, there is a BTI alarm, which gets
triggered when an external chip has crossed its limits. Usually, this is
connected to some LM75-like chip; if at least one crosses its limits, this
bit gets set.

If an alarm triggers, it will remain triggered until the hardware register
is read at least once. This means that the cause for the alarm may already
have disappeared! Note that in the current implementation, all hardware
registers are read whenever any data is read (unless it is less than 1.5
seconds since the last update). This means that you can easily miss
once-only alarms.

The SiS5595 only updates its values each 1.5 seconds; reading it more often
will do no harm, but will return 'old' values.

Problems
--------
Some chips refuse to be enabled. We don't know why.
The driver will recognize this and print a message in dmesg.
