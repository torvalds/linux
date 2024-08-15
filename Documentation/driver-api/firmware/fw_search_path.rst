=====================
Firmware search paths
=====================

The following search paths are used to look for firmware on your
root filesystem.

* fw_path_para - module parameter - default is empty so this is ignored
* /lib/firmware/updates/UTS_RELEASE/
* /lib/firmware/updates/
* /lib/firmware/UTS_RELEASE/
* /lib/firmware/

The module parameter ''path'' can be passed to the firmware_class module
to activate the first optional custom fw_path_para. The custom path can
only be up to 256 characters long. The kernel parameter passed would be:

* 'firmware_class.path=$CUSTOMIZED_PATH'

There is an alternative to customize the path at run time after bootup, you
can use the file:

* /sys/module/firmware_class/parameters/path

You would echo into it your custom path and firmware requested will be
searched for there first.
