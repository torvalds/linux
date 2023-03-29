.. SPDX-License-Identifier: GPL-2.0

===================
Firmware Upload API
===================

A device driver that registers with the firmware loader will expose
persistent sysfs nodes to enable users to initiate firmware updates for
that device.  It is the responsibility of the device driver and/or the
device itself to perform any validation on the data received. Firmware
upload uses the same *loading* and *data* sysfs files described in the
documentation for firmware fallback. It also adds additional sysfs files
to provide status on the transfer of the firmware image to the device.

Register for firmware upload
============================

A device driver registers for firmware upload by calling
firmware_upload_register(). Among the parameter list is a name to
identify the device under /sys/class/firmware. A user may initiate a
firmware upload by echoing a 1 to the *loading* sysfs file for the target
device. Next, the user writes the firmware image to the *data* sysfs
file. After writing the firmware data, the user echos 0 to the *loading*
sysfs file to signal completion. Echoing 0 to *loading* also triggers the
transfer of the firmware to the lower-lever device driver in the context
of a kernel worker thread.

To use the firmware upload API, write a driver that implements a set of
ops.  The probe function calls firmware_upload_register() and the remove
function calls firmware_upload_unregister() such as::

	static const struct fw_upload_ops m10bmc_ops = {
		.prepare = m10bmc_sec_prepare,
		.write = m10bmc_sec_write,
		.poll_complete = m10bmc_sec_poll_complete,
		.cancel = m10bmc_sec_cancel,
		.cleanup = m10bmc_sec_cleanup,
	};

	static int m10bmc_sec_probe(struct platform_device *pdev)
	{
		const char *fw_name, *truncate;
		struct m10bmc_sec *sec;
		struct fw_upload *fwl;
		unsigned int len;

		sec = devm_kzalloc(&pdev->dev, sizeof(*sec), GFP_KERNEL);
		if (!sec)
			return -ENOMEM;

		sec->dev = &pdev->dev;
		sec->m10bmc = dev_get_drvdata(pdev->dev.parent);
		dev_set_drvdata(&pdev->dev, sec);

		fw_name = dev_name(sec->dev);
		truncate = strstr(fw_name, ".auto");
		len = (truncate) ? truncate - fw_name : strlen(fw_name);
		sec->fw_name = kmemdup_nul(fw_name, len, GFP_KERNEL);

		fwl = firmware_upload_register(THIS_MODULE, sec->dev, sec->fw_name,
					       &m10bmc_ops, sec);
		if (IS_ERR(fwl)) {
			dev_err(sec->dev, "Firmware Upload driver failed to start\n");
			kfree(sec->fw_name);
			return PTR_ERR(fwl);
		}

		sec->fwl = fwl;
		return 0;
	}

	static int m10bmc_sec_remove(struct platform_device *pdev)
	{
		struct m10bmc_sec *sec = dev_get_drvdata(&pdev->dev);

		firmware_upload_unregister(sec->fwl);
		kfree(sec->fw_name);
		return 0;
	}

firmware_upload_register
------------------------
.. kernel-doc:: drivers/base/firmware_loader/sysfs_upload.c
   :identifiers: firmware_upload_register

firmware_upload_unregister
--------------------------
.. kernel-doc:: drivers/base/firmware_loader/sysfs_upload.c
   :identifiers: firmware_upload_unregister

Firmware Upload Ops
-------------------
.. kernel-doc:: include/linux/firmware.h
   :identifiers: fw_upload_ops

Firmware Upload Progress Codes
------------------------------
The following progress codes are used internally by the firmware loader.
Corresponding strings are reported through the status sysfs node that
is described below and are documented in the ABI documentation.

.. kernel-doc:: drivers/base/firmware_loader/sysfs_upload.h
   :identifiers: fw_upload_prog

Firmware Upload Error Codes
---------------------------
The following error codes may be returned by the driver ops in case of
failure:

.. kernel-doc:: include/linux/firmware.h
   :identifiers: fw_upload_err

Sysfs Attributes
================

In addition to the *loading* and *data* sysfs files, there are additional
sysfs files to monitor the status of the data transfer to the target
device and to determine the final pass/fail status of the transfer.
Depending on the device and the size of the firmware image, a firmware
update could take milliseconds or minutes.

The additional sysfs files are:

* status - provides an indication of the progress of a firmware update
* error - provides error information for a failed firmware update
* remaining_size - tracks the data transfer portion of an update
* cancel - echo 1 to this file to cancel the update
