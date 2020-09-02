.. SPDX-License-Identifier: GPL-2.0-only

==================================
PLDM Firmware file format overview
==================================

A PLDM firmware package is a binary file which contains a header that
describes the contents of the firmware package. This includes an initial
package header, one or more firmware records, and one or more components
describing the actual flash contents to program.

This diagram provides an overview of the file format::

        overall file layout
      +----------------------+
      |                      |
      |  Package Header      |
      |                      |
      +----------------------+
      |                      |
      |  Device Records      |
      |                      |
      +----------------------+
      |                      |
      |  Component Info      |
      |                      |
      +----------------------+
      |                      |
      |  Package Header CRC  |
      |                      |
      +----------------------+
      |                      |
      |  Component Image 1   |
      |                      |
      +----------------------+
      |                      |
      |  Component Image 2   |
      |                      |
      +----------------------+
      |                      |
      |         ...          |
      |                      |
      +----------------------+
      |                      |
      |  Component Image N   |
      |                      |
      +----------------------+

Package Header
==============

The package header begins with the UUID of the PLDM file format, and
contains information about the version of the format that the file uses. It
also includes the total header size, a release date, the size of the
component bitmap, and an overall package version.

The following diagram provides an overview of the package header::

             header layout
      +-------------------------+
      | PLDM UUID               |
      +-------------------------+
      | Format Revision         |
      +-------------------------+
      | Header Size             |
      +-------------------------+
      | Release Date            |
      +-------------------------+
      | Component Bitmap Length |
      +-------------------------+
      | Package Version Info    |
      +-------------------------+

Device Records
==============

The device firmware records area starts with a count indicating the total
number of records in the file, followed by each record. A single device
record describes what device matches this record. All valid PLDM firmware
files must contain at least one record, but optionally may contain more than
one record if they support multiple devices.

Each record will identify the device it supports via TLVs that describe the
device, such as the PCI device and vendor information. It will also indicate
which set of components that are used by this device. It is possible that
only subset of provided components will be used by a given record. A record
may also optionally contain device-specific package data that will be used
by the device firmware during the update process.

The following diagram provides an overview of the device record area::

         area layout
      +---------------+
      |               |
      |  Record Count |
      |               |
      +---------------+
      |               |
      |  Record 1     |
      |               |
      +---------------+
      |               |
      |  Record 2     |
      |               |
      +---------------+
      |               |
      |      ...      |
      |               |
      +---------------+
      |               |
      |  Record N     |
      |               |
      +---------------+

           record layout
      +-----------------------+
      | Record Length         |
      +-----------------------+
      | Descriptor Count      |
      +-----------------------+
      | Option Flags          |
      +-----------------------+
      | Version Settings      |
      +-----------------------+
      | Package Data Length   |
      +-----------------------+
      | Applicable Components |
      +-----------------------+
      | Version String        |
      +-----------------------+
      | Descriptor TLVs       |
      +-----------------------+
      | Package Data          |
      +-----------------------+

Component Info
==============

The component information area begins with a count of the number of
components. Following this count is a description for each component. The
component information points to the location in the file where the component
data is stored, and includes version data used to identify the version of
the component.

The following diagram provides an overview of the component area::

         area layout
      +-----------------+
      |                 |
      | Component Count |
      |                 |
      +-----------------+
      |                 |
      | Component 1     |
      |                 |
      +-----------------+
      |                 |
      | Component 2     |
      |                 |
      +-----------------+
      |                 |
      |     ...         |
      |                 |
      +-----------------+
      |                 |
      | Component N     |
      |                 |
      +-----------------+

           component layout
      +------------------------+
      | Classification         |
      +------------------------+
      | Component Identifier   |
      +------------------------+
      | Comparison Stamp       |
      +------------------------+
      | Component Options      |
      +------------------------+
      | Activation Method      |
      +------------------------+
      | Location Offset        |
      +------------------------+
      | Component Size         |
      +------------------------+
      | Component Version Info |
      +------------------------+
      | Package Data           |
      +------------------------+


Package Header CRC
==================

Following the component information is a short 4-byte CRC calculated over
the contents of all of the header information.

Component Images
================

The component images follow the package header information in the PLDM
firmware file. Each of these is simply a binary chunk with its start and
size defined by the matching component structure in the component info area.
