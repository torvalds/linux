Kernel driver vexpress
======================

Supported systems:

  * ARM Ltd. Versatile Express platform

    Prefix: 'vexpress'

    Datasheets:

      * "Hardware Description" sections of the Technical Reference Manuals
	for the Versatile Express boards:

	- http://infocenter.arm.com/help/topic/com.arm.doc.subset.boards.express/index.html

      * Section "4.4.14. System Configuration registers" of the V2M-P1 TRM:

	- http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.dui0447-/index.html

Author: Pawel Moll

Description
-----------

Versatile Express platform (http://www.arm.com/versatileexpress/) is a
reference & prototyping system for ARM Ltd. processors. It can be set up
from a wide range of boards, each of them containing (apart of the main
chip/FPGA) a number of microcontrollers responsible for platform
configuration and control. These microcontrollers can also monitor the
board and its environment by a number of internal and external sensors,
providing information about power lines voltages and currents, board
temperature and power usage. Some of them also calculate consumed energy
and provide a cumulative use counter.

The configuration devices are _not_ memory mapped and must be accessed
via a custom interface, abstracted by the "vexpress_config" API.

As these devices are non-discoverable, they must be described in a Device
Tree passed to the kernel. Details of the DT binding for them can be found
in Documentation/devicetree/bindings/hwmon/vexpress.txt.
