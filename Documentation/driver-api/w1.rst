======================
W1: Dallas' 1-wire bus
======================

:Author: David Fries

W1 API internal to the kernel
=============================

include/linux/w1.h
~~~~~~~~~~~~~~~~~~

W1 kernel API functions.

.. kernel-doc:: include/linux/w1.h
   :internal:

drivers/w1/w1.c
~~~~~~~~~~~~~~~

W1 core functions.

.. kernel-doc:: drivers/w1/w1.c
   :internal:

drivers/w1/w1_family.c
~~~~~~~~~~~~~~~~~~~~~~~

Allows registering device family operations.

.. kernel-doc:: drivers/w1/w1_family.c
   :export:

drivers/w1/w1_internal.h
~~~~~~~~~~~~~~~~~~~~~~~~

W1 internal initialization for master devices.

.. kernel-doc:: drivers/w1/w1_internal.h
   :internal:

drivers/w1/w1_int.c
~~~~~~~~~~~~~~~~~~~~

W1 internal initialization for master devices.

.. kernel-doc:: drivers/w1/w1_int.c
   :export:

drivers/w1/w1_netlink.h
~~~~~~~~~~~~~~~~~~~~~~~~

W1 external netlink API structures and commands.

.. kernel-doc:: drivers/w1/w1_netlink.h
   :internal:

drivers/w1/w1_io.c
~~~~~~~~~~~~~~~~~~~

W1 input/output.

.. kernel-doc:: drivers/w1/w1_io.c
   :export:

.. kernel-doc:: drivers/w1/w1_io.c
   :internal:
