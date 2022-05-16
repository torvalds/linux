.. SPDX-License-Identifier: GPL-2.0-only

=========================
Driver-specific callbacks
=========================

The ``pldmfw`` module relies on the device driver for implementing device
specific behavior using the following operations.

``.match_record``
-----------------

The ``.match_record`` operation is used to determine whether a given PLDM
record matches the device being updated. This requires comparing the record
descriptors in the record with information from the device. Many record
descriptors are defined by the PLDM standard, but it is also allowed for
devices to implement their own descriptors.

The ``.match_record`` operation should return true if a given record matches
the device.

``.send_package_data``
----------------------

The ``.send_package_data`` operation is used to send the device-specific
package data in a record to the device firmware. If the matching record
provides package data, ``pldmfw`` will call the ``.send_package_data``
function with a pointer to the package data and with the package data
length. The device driver should send this data to firmware.

``.send_component_table``
-------------------------

The ``.send_component_table`` operation is used to forward component
information to the device. It is called once for each applicable component,
that is, for each component indicated by the matching record. The
device driver should send the component information to the device firmware,
and wait for a response. The provided transfer flag indicates whether this
is the first, last, or a middle component, and is expected to be forwarded
to firmware as part of the component table information. The driver should an
error in the case when the firmware indicates that the component cannot be
updated, or return zero if the component can be updated.

``.flash_component``
--------------------

The ``.flash_component`` operation is used to inform the device driver to
flash a given component. The driver must perform any steps necessary to send
the component data to the device.

``.finalize_update``
--------------------

The ``.finalize_update`` operation is used by the ``pldmfw`` library in
order to allow the device driver to perform any remaining device specific
logic needed to finish the update.
