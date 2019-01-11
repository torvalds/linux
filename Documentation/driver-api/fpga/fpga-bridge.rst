FPGA Bridge
===========

API to implement a new FPGA bridge
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. kernel-doc:: include/linux/fpga/fpga-bridge.h
   :functions: fpga_bridge

.. kernel-doc:: include/linux/fpga/fpga-bridge.h
   :functions: fpga_bridge_ops

.. kernel-doc:: drivers/fpga/fpga-bridge.c
   :functions: fpga_bridge_create

.. kernel-doc:: drivers/fpga/fpga-bridge.c
   :functions: fpga_bridge_free

.. kernel-doc:: drivers/fpga/fpga-bridge.c
   :functions: fpga_bridge_register

.. kernel-doc:: drivers/fpga/fpga-bridge.c
   :functions: fpga_bridge_unregister

API to control an FPGA bridge
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You probably won't need these directly.  FPGA regions should handle this.

.. kernel-doc:: drivers/fpga/fpga-bridge.c
   :functions: of_fpga_bridge_get

.. kernel-doc:: drivers/fpga/fpga-bridge.c
   :functions: fpga_bridge_get

.. kernel-doc:: drivers/fpga/fpga-bridge.c
   :functions: fpga_bridge_put

.. kernel-doc:: drivers/fpga/fpga-bridge.c
   :functions: fpga_bridge_get_to_list

.. kernel-doc:: drivers/fpga/fpga-bridge.c
   :functions: of_fpga_bridge_get_to_list

.. kernel-doc:: drivers/fpga/fpga-bridge.c
   :functions: fpga_bridge_enable

.. kernel-doc:: drivers/fpga/fpga-bridge.c
   :functions: fpga_bridge_disable
