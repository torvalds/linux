.. SPDX-License-Identifier: GPL-2.0+

.. |san_client_link| replace:: :c:func:`san_client_link`
.. |san_dgpu_notifier_register| replace:: :c:func:`san_dgpu_notifier_register`
.. |san_dgpu_notifier_unregister| replace:: :c:func:`san_dgpu_notifier_unregister`

===================
Surface ACPI Notify
===================

The Surface ACPI Notify (SAN) device provides the bridge between ACPI and
SAM controller. Specifically, ACPI code can execute requests and handle
battery and thermal events via this interface. In addition to this, events
relating to the discrete GPU (dGPU) of the Surface Book 2 can be sent from
ACPI code (note: the Surface Book 3 uses a different method for this). The
only currently known event sent via this interface is a dGPU power-on
notification. While this driver handles the former part internally, it only
relays the dGPU events to any other driver interested via its public API and
does not handle them.

The public interface of this driver is split into two parts: Client
registration and notifier-block registration.

A client to the SAN interface can be linked as consumer to the SAN device
via |san_client_link|. This can be used to ensure that the a client
receiving dGPU events does not miss any events due to the SAN interface not
being set up as this forces the client driver to unbind once the SAN driver
is unbound.

Notifier-blocks can be registered by any device for as long as the module is
loaded, regardless of being linked as client or not. Registration is done
with |san_dgpu_notifier_register|. If the notifier is not needed any more, it
should be unregistered via |san_dgpu_notifier_unregister|.

Consult the API documentation below for more details.


API Documentation
=================

.. kernel-doc:: include/linux/surface_acpi_notify.h

.. kernel-doc:: drivers/platform/surface/surface_acpi_notify.c
    :export:
