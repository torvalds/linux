.. SPDX-License-Identifier: GPL-2.0-or-later

========================================
Uniwill Notebook driver (uniwill-laptop)
========================================

Introduction
============

Many notebooks manufactured by Uniwill (either directly or as ODM) provide a EC interface
for controlling various platform settings like sensors and fan control. This interface is
used by the ``uniwill-laptop`` driver to map those features onto standard kernel interfaces.

EC WMI interface description
============================

The EC WMI interface description can be decoded from the embedded binary MOF (bmof)
data using the `bmfdec <https://github.com/pali/bmfdec>`_ utility:

::

  [WMI, Dynamic, Provider("WmiProv"), Locale("MS\\0x409"),
   Description("Class used to operate methods on a ULong"),
   guid("{ABBC0F6F-8EA1-11d1-00A0-C90629100000}")]
  class AcpiTest_MULong {
    [key, read] string InstanceName;
    [read] boolean Active;

    [WmiMethodId(1), Implemented, read, write, Description("Return the contents of a ULong")]
    void GetULong([out, Description("Ulong Data")] uint32 Data);

    [WmiMethodId(2), Implemented, read, write, Description("Set the contents of a ULong")]
    void SetULong([in, Description("Ulong Data")] uint32 Data);

    [WmiMethodId(3), Implemented, read, write,
     Description("Generate an event containing ULong data")]
    void FireULong([in, Description("WMI requires a parameter")] uint32 Hack);

    [WmiMethodId(4), Implemented, read, write, Description("Get and Set the contents of a ULong")]
    void GetSetULong([in, Description("Ulong Data")] uint64 Data,
                     [out, Description("Ulong Data")] uint32 Return);

    [WmiMethodId(5), Implemented, read, write,
     Description("Get and Set the contents of a ULong for Dollby button")]
    void GetButton([in, Description("Ulong Data")] uint64 Data,
                   [out, Description("Ulong Data")] uint32 Return);
  };

Most of the WMI-related code was copied from the Windows driver samples, which unfortunately means
that the WMI-GUID is not unique. This makes the WMI-GUID unusable for autoloading.

WMI method GetULong()
---------------------

This WMI method was copied from the Windows driver samples and has no function.

WMI method SetULong()
---------------------

This WMI method was copied from the Windows driver samples and has no function.

WMI method FireULong()
----------------------

This WMI method allows to inject a WMI event with a 32-bit payload. Its primary purpose seems
to be debugging.

WMI method GetSetULong()
------------------------

This WMI method is used to communicate with the EC. The ``Data`` argument holds the following
information (starting with the least significant byte):

1. 16-bit address
2. 16-bit data (set to ``0x0000`` when reading)
3. 16-bit operation (``0x0100`` for reading and ``0x0000`` for writing)
4. 16-bit reserved (set to ``0x0000``)

The first 8 bits of the ``Return`` value contain the data returned by the EC when reading.
The special value ``0xFEFEFEFE`` is used to indicate a communication failure with the EC.

WMI method GetButton()
----------------------

This WMI method is not implemented on all machines and has an unknown purpose.

Reverse-Engineering the EC WMI interface
========================================

.. warning:: Randomly poking the EC can potentially cause damage to the machine and other unwanted
             side effects, please be careful.

The EC behind the ``GetSetULong`` method is used by the OEM software supplied by the manufacturer.
Reverse-engineering of this software is difficult since it uses an obfuscator, however some parts
are not obfuscated. In this case `dnSpy <https://github.com/dnSpy/dnSpy>`_ could also be helpful.

The EC can be accessed under Windows using powershell (requires admin privileges):

::

  > $obj = Get-CimInstance -Namespace root/wmi -ClassName AcpiTest_MULong | Select-Object -First 1
  > Invoke-CimMethod -InputObject $obj -MethodName GetSetULong -Arguments @{Data = <input>}

WMI event interface description
===============================

The WMI interface description can also be decoded from the embedded binary MOF (bmof)
data:

::

  [WMI, Dynamic, Provider("WmiProv"), Locale("MS\\0x409"),
   Description("Class containing event generated ULong data"),
   guid("{ABBC0F72-8EA1-11d1-00A0-C90629100000}")]
  class AcpiTest_EventULong : WmiEvent {
    [key, read] string InstanceName;
    [read] boolean Active;

    [WmiDataId(1), read, write, Description("ULong Data")] uint32 ULong;
  };

Most of the WMI-related code was again copied from the Windows driver samples, causing this WMI
interface to suffer from the same restrictions as the EC WMI interface described above.

WMI event data
--------------

The WMI event data contains a single 32-bit value which is used to indicate various platform events.

Reverse-Engineering the Uniwill WMI event interface
===================================================

The driver logs debug messages when receiving a WMI event. Thus enabling debug messages will be
useful for finding unknown event codes.

EC ACPI interface description
=============================

The ``INOU0000`` ACPI device is a virtual device used to access various hardware registers
available on notebooks manufactured by Uniwill. Reading and writing those registers happens
by calling ACPI control methods. The ``uniwill-laptop`` driver uses this device to communicate
with the EC because the ACPI control methods are faster than the WMI methods described above.

ACPI control methods used for reading registers take a single ACPI integer containing the address
of the register to read and return a ACPI integer containing the data inside said register. ACPI
control methods used for writing registers however take two ACPI integers, with the additional
ACPI integer containing the data to be written into the register. Such ACPI control methods return
nothing.

System memory
-------------

System memory can be accessed with a granularity of either a single byte (``MMRB`` for reading and
``MMWB`` for writing) or four bytes (``MMRD`` for reading and ``MMWD`` for writing). Those ACPI
control methods are unused because they provide no benefit when compared to the native memory
access functions provided by the kernel.

EC RAM
------

The internal RAM of the EC can be accessed with a granularity of a single byte using the ``ECRR``
(read) and ``ECRW`` (write) ACPI control methods, with the maximum register address being ``0xFFF``.
The OEM software waits 6 ms after calling one of those ACPI control methods, likely to avoid
overwhelming the EC when being connected over LPC.

PCI config space
----------------

The PCI config space can be accessed with a granularity of four bytes using the ``PCRD`` (read) and
``PCWD`` (write) ACPI control methods. The exact address format is unknown, and poking random PCI
devices might confuse the PCI subsystem. Because of this those ACPI control methods are not used.

IO ports
--------

IO ports can be accessed with a granularity of four bytes using the ``IORD`` (read) and ``IOWD``
(write) ACPI control methods. Those ACPI control methods are unused because they provide no benefit
when compared to the native IO port access functions provided by the kernel.

CMOS RAM
--------

The CMOS RAM can be accessed with a granularity of a single byte using the ``RCMS`` (read) and
``WCMS`` ACPI control methods. Using those ACPI methods might interfere with the native CMOS RAM
access functions provided by the kernel due to the usage of indexed IO, so they are unused.

Indexed IO
----------

Indexed IO with IO ports with a granularity of a single byte can be performed using the ``RIOP``
(read) and ``WIOP`` (write) ACPI control methods. Those ACPI methods are unused because they
provide no benifit when compared to the native IO port access functions provided by the kernel.

Special thanks go to github user `pobrn` which developed the
`qc71_laptop <https://github.com/pobrn/qc71_laptop>`_ driver on which this driver is partly based.
The same is true for Tuxedo Computers, which developed the
`tuxedo-drivers <https://gitlab.com/tuxedocomputers/development/packages/tuxedo-drivers>`_ package
which also served as a foundation for this driver.
