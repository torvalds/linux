===================
Firmware Guidelines
===================

Users switching to a newer kernel should *not* have to install newer
firmware files to keep their hardware working. At the same time updated
firmware files must not cause any regressions for users of older kernel
releases.

Drivers that use firmware from linux-firmware should follow the rules in
this guide. (Where there is limited control of the firmware,
i.e. company doesn't support Linux, firmwares sourced from misc places,
then of course these rules will not apply strictly.)

* Firmware files shall be designed in a way that it allows checking for
  firmware ABI version changes. It is recommended that firmware files be
  versioned with at least a major/minor version. It is suggested that
  the firmware files in linux-firmware be named with some device
  specific name, and just the major version. The firmware version should
  be stored in the firmware header, or as an exception, as part of the
  firmware file name, in order to let the driver detact any non-ABI
  fixes/changes. The firmware files in linux-firmware should be
  overwritten with the newest compatible major version. Newer major
  version firmware shall remain compatible with all kernels that load
  that major number.

* If the kernel support for the hardware is normally inactive, or the
  hardware isn't available for public consumption, this can
  be ignored, until the first kernel release that enables that hardware.
  This means no major version bumps without the kernel retaining
  backwards compatibility for the older major versions.  Minor version
  bumps should not introduce new features that newer kernels depend on
  non-optionally.

* If a security fix needs lockstep firmware and kernel fixes in order to
  be successful, then all supported major versions in the linux-firmware
  repo that are required by currently supported stable/LTS kernels,
  should be updated with the security fix. The kernel patches should
  detect if the firmware is new enough to declare if the security issue
  is fixed.  All communications around security fixes should point at
  both the firmware and kernel fixes. If a security fix requires
  deprecating old major versions, then this should only be done as a
  last option, and be stated clearly in all communications.

* Firmware files that affect the User API (UAPI) shall not introduce
  changes that break existing userspace programs. Updates to such firmware
  must ensure backward compatibility with existing userspace applications.
  This includes maintaining consistent interfaces and behaviors that
  userspace programs rely on.
