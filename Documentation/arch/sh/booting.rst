.. SPDX-License-Identifier: GPL-2.0

DeviceTree Booting
------------------

  Device-tree compatible SH bootloaders are expected to provide the physical
  address of the device tree blob in r4. Since legacy bootloaders did not
  guarantee any particular initial register state, kernels built to
  inter-operate with old bootloaders must either use a builtin DTB or
  select a legacy board option (something other than CONFIG_SH_DEVICE_TREE)
  that does not use device tree. Support for the latter is being phased out
  in favor of device tree.
