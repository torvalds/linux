.. SPDX-License-Identifier: GPL-2.0

========================
tcm_qla2xxx Driver Notes
========================

tcm_qla2xxx jam_host attribute
------------------------------
There is now a new module endpoint atribute called jam_host
attribute::

	jam_host: boolean=0/1

This attribute and accompanying code is only included if the
Kconfig parameter TCM_QLA2XXX_DEBUG is set to Y

By default this jammer code and functionality is disabled

Use this attribute to control the discarding of SCSI commands to a
selected host.

This may be useful for testing error handling and simulating slow drain
and other fabric issues.

Setting a boolean of 1 for the jam_host attribute for a particular host
will discard the commands for that host.

Reset back to 0 to stop the jamming.

Enable host 4 to be jammed::

  echo 1 > /sys/kernel/config/target/qla2xxx/21:00:00:24:ff:27:8f:ae/tpgt_1/attrib/jam_host

Disable jamming on host 4::

  echo 0 > /sys/kernel/config/target/qla2xxx/21:00:00:24:ff:27:8f:ae/tpgt_1/attrib/jam_host
