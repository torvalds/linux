NVEC: An NVidia compliant Embedded Controller Protocol Implementation

This is an implementation of the NVEC protocol used to communicate with an
embedded controller (EC) via I2C bus. The EC is an I2C master while the host
processor is the I2C slave. Requests from the host processor to the EC are
started by triggering a gpio line.

There is no written documentation of the protocol available to the public,
but the source code[1] of the published nvec reference drivers can be a guide.
This driver is currently only used by the AC100 project[2], but it is likely,
that other Tegra boards (not yet mainlined, if ever) also use it.

[1] e.g. https://nv-tegra.nvidia.com/gitweb/?p=linux-2.6.git;a=tree;f=arch/arm/mach-tegra/nvec;hb=android-tegra-2.6.32
[2] http://gitorious.org/ac100, http://launchpad.net/ac100
