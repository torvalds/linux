.. SPDX-License-Identifier: GPL-2.0-only

==================================
PLDM Firmware Flash Update Library
==================================

``pldmfw`` implements functionality for updating the flash on a device using
the PLDM for Firmware Update standard
<https://www.dmtf.org/documents/pmci/pldm-firmware-update-specification-100>.

.. toctree::
   :maxdepth: 1

   file-format
   driver-ops

==================================
Overview of the ``pldmfw`` library
==================================

The ``pldmfw`` library is intended to be used by device drivers for
implementing device flash update based on firmware files following the PLDM
firwmare file format.

It is implemented using an ops table that allows device drivers to provide
the underlying device specific functionality.

``pldmfw`` implements logic to parse the packed binary format of the PLDM
firmware file into data structures, and then uses the provided function
operations to determine if the firmware file is a match for the device. If
so, it sends the record and component data to the firmware using the device
specific implementations provided by device drivers. Once the device
firmware indicates that the update may be performed, the firmware data is
sent to the device for programming.

Parsing the PLDM file
=====================

The PLDM file format uses packed binary data, with most multi-byte fields
stored in the Little Endian format. Several pieces of data are variable
length, including version strings and the number of records and components.
Due to this, it is not straight forward to index the record, record
descriptors, or components.

To avoid proliferating access to the packed binary data, the ``pldmfw``
library parses and extracts this data into simpler structures for ease of
access.

In order to safely process the firmware file, care is taken to avoid
unaligned access of multi-byte fields, and to properly convert from Little
Endian to CPU host format. Additionally the records, descriptors, and
components are stored in linked lists.

Performing a flash update
=========================

To perform a flash update, the ``pldmfw`` module performs the following
steps

1. Parse the firmware file for record and component information
2. Scan through the records and determine if the device matches any record
   in the file. The first matched record will be used.
3. If the matching record provides package data, send this package data to
   the device.
4. For each component that the record indicates, send the component data to
   the device. For each component, the firmware may respond with an
   indication of whether the update is suitable or not. If any component is
   not suitable, the update is canceled.
5. For each component, send the binary data to the device firmware for
   updating.
6. After all components are programmed, perform any final device-specific
   actions to finalize the update.
