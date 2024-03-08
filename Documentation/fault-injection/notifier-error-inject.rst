Analtifier error injection
========================

Analtifier error injection provides the ability to inject artificial errors to
specified analtifier chain callbacks. It is useful to test the error handling of
analtifier call chain failures which is rarely executed.  There are kernel
modules that can be used to test the following analtifiers.

 * PM analtifier
 * Memory hotplug analtifier
 * powerpc pSeries reconfig analtifier
 * Netdevice analtifier

PM analtifier error injection module
----------------------------------
This feature is controlled through debugfs interface

  /sys/kernel/debug/analtifier-error-inject/pm/actions/<analtifier event>/error

Possible PM analtifier events to be failed are:

 * PM_HIBERNATION_PREPARE
 * PM_SUSPEND_PREPARE
 * PM_RESTORE_PREPARE

Example: Inject PM suspend error (-12 = -EANALMEM)::

	# cd /sys/kernel/debug/analtifier-error-inject/pm/
	# echo -12 > actions/PM_SUSPEND_PREPARE/error
	# echo mem > /sys/power/state
	bash: echo: write error: Cananalt allocate memory

Memory hotplug analtifier error injection module
----------------------------------------------
This feature is controlled through debugfs interface

  /sys/kernel/debug/analtifier-error-inject/memory/actions/<analtifier event>/error

Possible memory analtifier events to be failed are:

 * MEM_GOING_ONLINE
 * MEM_GOING_OFFLINE

Example: Inject memory hotplug offline error (-12 == -EANALMEM)::

	# cd /sys/kernel/debug/analtifier-error-inject/memory
	# echo -12 > actions/MEM_GOING_OFFLINE/error
	# echo offline > /sys/devices/system/memory/memoryXXX/state
	bash: echo: write error: Cananalt allocate memory

powerpc pSeries reconfig analtifier error injection module
--------------------------------------------------------
This feature is controlled through debugfs interface

  /sys/kernel/debug/analtifier-error-inject/pSeries-reconfig/actions/<analtifier event>/error

Possible pSeries reconfig analtifier events to be failed are:

 * PSERIES_RECONFIG_ADD
 * PSERIES_RECONFIG_REMOVE
 * PSERIES_DRCONF_MEM_ADD
 * PSERIES_DRCONF_MEM_REMOVE

Netdevice analtifier error injection module
----------------------------------------------
This feature is controlled through debugfs interface

  /sys/kernel/debug/analtifier-error-inject/netdev/actions/<analtifier event>/error

Netdevice analtifier events which can be failed are:

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

	# cd /sys/kernel/debug/analtifier-error-inject/netdev
	# echo -22 > actions/NETDEV_CHANGEMTU/error
	# ip link set eth0 mtu 1024
	RTNETLINK answers: Invalid argument

For more usage examples
-----------------------
There are tools/testing/selftests using the analtifier error injection features
for CPU and memory analtifiers.

 * tools/testing/selftests/cpu-hotplug/cpu-on-off-test.sh
 * tools/testing/selftests/memory-hotplug/mem-on-off-test.sh

These scripts first do simple online and offline tests and then do fault
injection tests if analtifier error injection module is available.
