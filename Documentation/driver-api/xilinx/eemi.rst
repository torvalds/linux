====================================
Xilinx Zynq MPSoC EEMI Documentation
====================================

Xilinx Zynq MPSoC Firmware Interface
-------------------------------------
The zynqmp-firmware node describes the interface to platform firmware.
ZynqMP has an interface to communicate with secure firmware. Firmware
driver provides an interface to firmware APIs. Interface APIs can be
used by any driver to communicate with PMC(Platform Management Controller).

Embedded Energy Management Interface (EEMI)
----------------------------------------------
The embedded energy management interface is used to allow software
components running across different processing clusters on a chip or
device to communicate with a power management controller (PMC) on a
device to issue or respond to power management requests.

Any driver who wants to communicate with PMC using EEMI APIs use the
functions provided for each function.

IOCTL
------
IOCTL API is for device control and configuration. It is not a system
IOCTL but it is an EEMI API. This API can be used by master to control
any device specific configuration. IOCTL definitions can be platform
specific. This API also manage shared device configuration.

The following IOCTL IDs are valid for device control:
- IOCTL_SET_PLL_FRAC_MODE	8
- IOCTL_GET_PLL_FRAC_MODE	9
- IOCTL_SET_PLL_FRAC_DATA	10
- IOCTL_GET_PLL_FRAC_DATA	11

Refer EEMI API guide [0] for IOCTL specific parameters and other EEMI APIs.

References
----------
[0] Embedded Energy Management Interface (EEMI) API guide:
    https://www.xilinx.com/support/documentation/user_guides/ug1200-eemi-api.pdf
