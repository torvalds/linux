====================
request_firmware API
====================

You would typically load firmware and then load it into your device somehow.
The typical firmware work flow is reflected below::

	 if(request_firmware(&fw_entry, $FIRMWARE, device) == 0)
                copy_fw_to_device(fw_entry->data, fw_entry->size);
	 release_firmware(fw_entry);

Synchronous firmware requests
=============================

Synchronous firmware requests will wait until the firmware is found or until
an error is returned.

request_firmware
----------------
.. kernel-doc:: drivers/base/firmware_class.c
   :functions: request_firmware

request_firmware_direct
-----------------------
.. kernel-doc:: drivers/base/firmware_class.c
   :functions: request_firmware_direct

request_firmware_into_buf
-------------------------
.. kernel-doc:: drivers/base/firmware_class.c
   :functions: request_firmware_into_buf

Asynchronous firmware requests
==============================

Asynchronous firmware requests allow driver code to not have to wait
until the firmware or an error is returned. Function callbacks are
provided so that when the firmware or an error is found the driver is
informed through the callback. request_firmware_nowait() cannot be called
in atomic contexts.

request_firmware_nowait
-----------------------
.. kernel-doc:: drivers/base/firmware_class.c
   :functions: request_firmware_nowait

Considerations for suspend and resume
=====================================

During suspend and resume only the built-in firmware and the firmware cache
elements of the firmware API can be used. This is managed by fw_pm_notify().

fw_pm_notify
------------
.. kernel-doc:: drivers/base/firmware_class.c
   :functions: fw_pm_notify

request firmware API expected driver use
========================================

Once an API call returns you process the firmware and then release the
firmware. For example if you used request_firmware() and it returns,
the driver has the firmware image accessible in fw_entry->{data,size}.
If something went wrong request_firmware() returns non-zero and fw_entry
is set to NULL. Once your driver is done with processing the firmware it
can call call release_firmware(fw_entry) to release the firmware image
and any related resource.
