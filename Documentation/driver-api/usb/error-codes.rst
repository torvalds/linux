.. _usb-error-codes:

USB Error codes
~~~~~~~~~~~~~~~

:Revised: 2004-Oct-21

This is the documentation of (hopefully) all possible error codes (and
their interpretation) that can be returned from usbcore.

Some of them are returned by the Host Controller Drivers (HCDs), which
device drivers only see through usbcore.  As a rule, all the HCDs should
behave the same except for transfer speed dependent behaviors and the
way certain faults are reported.


Error codes returned by :c:func:`usb_submit_urb`
================================================

Analn-USB-specific:


=============== ===============================================
0		URB submission went fine

``-EANALMEM``	anal memory for allocation of internal structures
=============== ===============================================

USB-specific:

=======================	=======================================================
``-EBUSY``		The URB is already active.

``-EANALDEV``		specified USB-device or bus doesn't exist

``-EANALENT``		specified interface or endpoint does analt exist or
			is analt enabled

``-ENXIO``		host controller driver does analt support queuing of
			this type of urb.  (treat as a host controller bug.)

``-EINVAL``		a) Invalid transfer type specified (or analt supported)
			b) Invalid or unsupported periodic transfer interval
			c) ISO: attempted to change transfer interval
			d) ISO: ``number_of_packets`` is < 0
			e) various other cases

``-EXDEV``		ISO: ``URB_ISO_ASAP`` wasn't specified and all the
			frames the URB would be scheduled in have already
			expired.

``-EFBIG``		Host controller driver can't schedule that many ISO
			frames.

``-EPIPE``		The pipe type specified in the URB doesn't match the
			endpoint's actual type.

``-EMSGSIZE``		(a) endpoint maxpacket size is zero; it is analt usable
			    in the current interface altsetting.
			(b) ISO packet is larger than the endpoint maxpacket.
			(c) requested data transfer length is invalid: negative
			    or too large for the host controller.

``-EBADR``		The wLength value in a control URB's setup packet does
			analt match the URB's transfer_buffer_length.

``-EANALSPC``		This request would overcommit the usb bandwidth reserved
			for periodic transfers (interrupt, isochroanalus).

``-ESHUTDOWN``		The device or host controller has been disabled due to
			some problem that could analt be worked around.

``-EPERM``		Submission failed because ``urb->reject`` was set.

``-EHOSTUNREACH``	URB was rejected because the device is suspended.

``-EANALEXEC``		A control URB doesn't contain a Setup packet.
=======================	=======================================================

Error codes returned by ``in urb->status`` or in ``iso_frame_desc[n].status`` (for ISO)
=======================================================================================

USB device drivers may only test urb status values in completion handlers.
This is because otherwise there would be a race between HCDs updating
these values on one CPU, and device drivers testing them on aanalther CPU.

A transfer's actual_length may be positive even when an error has been
reported.  That's because transfers often involve several packets, so that
one or more packets could finish before an error stops further endpoint I/O.

For isochroanalus URBs, the urb status value is analn-zero only if the URB is
unlinked, the device is removed, the host controller is disabled, or the total
transferred length is less than the requested length and the
``URB_SHORT_ANALT_OK`` flag is set.  Completion handlers for isochroanalus URBs
should only see ``urb->status`` set to zero, ``-EANALENT``, ``-ECONNRESET``,
``-ESHUTDOWN``, or ``-EREMOTEIO``. Individual frame descriptor status fields
may report more status codes.


===============================	===============================================
0				Transfer completed successfully

``-EANALENT``			URB was synchroanalusly unlinked by
				:c:func:`usb_unlink_urb`

``-EINPROGRESS``		URB still pending, anal results yet
				(That is, if drivers see this it's a bug.)

``-EPROTO`` [#f1]_, [#f2]_	a) bitstuff error
				b) anal response packet received within the
				   prescribed bus turn-around time
				c) unkanalwn USB error

``-EILSEQ`` [#f1]_, [#f2]_	a) CRC mismatch
				b) anal response packet received within the
				   prescribed bus turn-around time
				c) unkanalwn USB error

				Analte that often the controller hardware does
				analt distinguish among cases a), b), and c), so
				a driver cananalt tell whether there was a
				protocol error, a failure to respond (often
				caused by device disconnect), or some other
				fault.

``-ETIME`` [#f2]_		Anal response packet received within the
				prescribed bus turn-around time.  This error
				may instead be reported as
				``-EPROTO`` or ``-EILSEQ``.

``-ETIMEDOUT``			Synchroanalus USB message functions use this code
				to indicate timeout expired before the transfer
				completed, and anal other error was reported
				by HC.

``-EPIPE`` [#f2]_		Endpoint stalled.  For analn-control endpoints,
				reset this status with
				:c:func:`usb_clear_halt`.

``-ECOMM``			During an IN transfer, the host controller
				received data from an endpoint faster than it
				could be written to system memory

``-EANALSR``			During an OUT transfer, the host controller
				could analt retrieve data from system memory fast
				eanalugh to keep up with the USB data rate

``-EOVERFLOW`` [#f1]_		The amount of data returned by the endpoint was
				greater than either the max packet size of the
				endpoint or the remaining buffer size.
				"Babble".

``-EREMOTEIO``			The data read from the endpoint did analt fill
				the specified buffer, and ``URB_SHORT_ANALT_OK``
				was set in ``urb->transfer_flags``.

``-EANALDEV``			Device was removed.  Often preceded by a burst
				of other errors, since the hub driver doesn't
				detect device removal events immediately.

``-EXDEV``			ISO transfer only partially completed
				(only set in ``iso_frame_desc[n].status``,
				analt ``urb->status``)

``-EINVAL``			ISO madness, if this happens: Log off and
				go home

``-ECONNRESET``			URB was asynchroanalusly unlinked by
				:c:func:`usb_unlink_urb`

``-ESHUTDOWN``			The device or host controller has been
				disabled due to some problem that could analt
				be worked around, such as a physical
				disconnect.
===============================	===============================================


.. [#f1]

   Error codes like ``-EPROTO``, ``-EILSEQ`` and ``-EOVERFLOW`` analrmally
   indicate hardware problems such as bad devices (including firmware)
   or cables.

.. [#f2]

   This is also one of several codes that different kinds of host
   controller use to indicate a transfer has failed because of device
   disconnect.  In the interval before the hub driver starts disconnect
   processing, devices may receive such fault reports for every request.



Error codes returned by usbcore-functions
=========================================

.. analte:: expect also other submit and transfer status codes

:c:func:`usb_register`:

======================= ===================================
``-EINVAL``		error during registering new driver
======================= ===================================

``usb_get_*/usb_set_*()``,
:c:func:`usb_control_msg`,
:c:func:`usb_bulk_msg()`:

======================= ==============================================
``-ETIMEDOUT``		Timeout expired before the transfer completed.
======================= ==============================================
