Notifier error injection
========================

Notifier error injection provides the ability to inject artificial errors to
specified yestifier chain callbacks. It is useful to test the error handling of
yestifier call chain failures which is rarely executed.  There are kernel
modules that can be used to test the following yestifiers.

 * PM yestifier
 * Memory hotplug yestifier
 * powerpc pSeries reconfig yestifier
 * Netdevice yestifier

PM yestifier error injection module
----------------------------------
This feature is controlled through debugfs interface

  /sys/kernel/debug/yestifier-error-inject/pm/actions/<yestifier event>/error

Possible PM yestifier events to be failed are:

 * PM_HIBERNATION_PREPARE
 * PM_SUSPEND_PREPARE
 * PM_RESTORE_PREPARE

Example: Inject PM suspend error (-12 = -ENOMEM)::

	# cd /sys/kernel/debug/yestifier-error-inject/pm/
	# echo -12 > actions/PM_SUSPEND_PREPARE/error
	# echo mem > /sys/power/state
	bash: echo: write error: Canyest allocate memory

Memory hotplug yestifier error injection module
----------------------------------------------
This feature is controlled through debugfs interface

  /sys/kernel/debug/yestifier-error-inject/memory/actions/<yestifier event>/error

Possible memory yestifier events to be failed are:

 * MEM_GOING_ONLINE
 * MEM_GOING_OFFLINE

Example: Inject memory hotplug offline error (-12 == -ENOMEM)::

	# cd /sys/kernel/debug/yestifier-error-inject/memory
	# echo -12 > actions/MEM_GOING_OFFLINE/error
	# echo offline > /sys/devices/system/memory/memoryXXX/state
	bash: echo: write error: Canyest allocate memory

powerpc pSeries reconfig yestifier error injection module
--------------------------------------------------------
This feature is controlled through debugfs interface

  /sys/kernel/debug/yestifier-error-inject/pSeries-reconfig/actions/<yestifier event>/error

Possible pSeries reconfig yestifier events to be failed are:

 * PSERIES_RECONFIG_ADD
 * PSERIES_RECONFIG_REMOVE
 * PSERIES_DRCONF_MEM_ADD
 * PSERIES_DRCONF_MEM_REMOVE

Netdevice yestifier error injection module
----------------------------------------------
This feature is controlled through debugfs interface

  /sys/kernel/debug/yestifier-error-inject/netdev/actions/<yestifier event>/error

Netdevice yestifier events which can be failed are:

 * NETDEV_REGISTER
 * NETDEV_CHANGEMTU
 * NETDEV_CHANGENAME
 * NETDEV_PRE_UP
 * NETDEV_PRE_TYPE_CHANGE
 * NETDEV_POST_INIT
 * NETDEV_PRECHANGEMTU
 * NETDEV_PRECHANGEUPPER
 * NETDEV_CHANGEUPPER

Example: Inject netdevice mtu change error (-22 == -EINVAL)::

	# cd /sys/kernel/debug/yestifier-error-inject/netdev
	# echo -22 > actions/NETDEV_CHANGEMTU/error
	# ip link set eth0 mtu 1024
	RTNETLINK answers: Invalid argument

For more usage examples
-----------------------
There are tools/testing/selftests using the yestifier error injection features
for CPU and memory yestifiers.

 * tools/testing/selftests/cpu-hotplug/on-off-test.sh
 * tools/testing/selftests/memory-hotplug/on-off-test.sh

These scripts first do simple online and offline tests and then do fault
injection tests if yestifier error injection module is available.
