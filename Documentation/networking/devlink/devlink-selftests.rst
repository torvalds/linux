.. SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)

=================
Devlink Selftests
=================

The ``devlink-selftests`` API allows executing selftests on the device.

Tests Mask
==========
The ``devlink-selftests`` command should be run with a mask indicating
the tests to be executed.

Tests Description
=================
The following is a list of tests that drivers may execute.

.. list-table:: List of tests
   :widths: 5 90

   * - Name
     - Description
   * - ``DEVLINK_SELFTEST_FLASH``
     - Devices may have the firmware on non-volatile memory on the board, e.g.
       flash. This particular test helps to run a flash selftest on the device.
       Implementation of the test is left to the driver/firmware.

example usage
-------------

.. code:: shell

    # Query selftests supported on the devlink device
    $ devlink dev selftests show DEV
    # Query selftests supported on all devlink devices
    $ devlink dev selftests show
    # Executes selftests on the device
    $ devlink dev selftests run DEV id flash
