FPGA Bridge
===========

API to implement a new FPGA bridge
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* struct fpga_bridge - The FPGA Bridge structure
* struct fpga_bridge_ops - Low level Bridge driver ops
* fpga_bridge_register() - Create and register a bridge
* fpga_bridge_unregister() - Unregister a bridge

.. kernel-doc:: include/linux/fpga/fpga-bridge.h
   :functions: fpga_bridge

.. kernel-doc:: include/linux/fpga/fpga-bridge.h
   :functions: fpga_bridge_ops

.. kernel-doc:: drivers/fpga/fpga-bridge.c
   :functions: fpga_bridge_register

.. kernel-doc:: drivers/fpga/fpga-bridge.c
   :functions: fpga_bridge_unregister
