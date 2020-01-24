===============================
Generic Block Device Capability
===============================

This file documents the sysfs file block/<disk>/capability

capability is a hex word indicating which capabilities a specific disk
supports.  For more information on bits not listed here, see
include/linux/genhd.h

GENHD_FL_MEDIA_CHANGE_NOTIFY
----------------------------

Value: 4

When this bit is set, the disk supports Asynchronous Notification
of media change events.  These events will be broadcast to user
space via kernel uevent.
