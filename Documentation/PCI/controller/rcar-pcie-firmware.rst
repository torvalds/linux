.. SPDX-License-Identifier: GPL-2.0

=================================================
Firmware of PCIe controller for Renesas R-Car V4H
=================================================

Renesas R-Car V4H (r8a779g0) has a PCIe controller, requiring a specific
firmware download during startup.

However, Renesas currently cannot distribute the firmware free of charge.

The firmware file "104_PCIe_fw_addr_data_ver1.05.txt" (note that the file name
might be different between different datasheet revisions) can be found in the
datasheet encoded as text, and as such, the file's content must be converted
back to binary form. This can be achieved using the following example script:

.. code-block:: sh

	$ awk '/^\s*0x[0-9A-Fa-f]{4}\s+0x[0-9A-Fa-f]{4}/ { print substr($2,5,2) substr($2,3,2) }' \
		104_PCIe_fw_addr_data_ver1.05.txt | \
			xxd -p -r > rcar_gen4_pcie.bin

Once the text content has been converted into a binary firmware file, verify
its checksum as follows:

.. code-block:: sh

	$ sha1sum rcar_gen4_pcie.bin
	1d0bd4b189b4eb009f5d564b1f93a79112994945  rcar_gen4_pcie.bin

The resulting binary file called "rcar_gen4_pcie.bin" should be placed in the
"/lib/firmware" directory before the driver runs.
