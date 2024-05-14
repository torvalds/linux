=========================
Kernel driver i2c-pca-isa
=========================

Supported adapters:

This driver supports ISA boards using the Philips PCA 9564
Parallel bus to I2C bus controller

Author: Ian Campbell <icampbell@arcom.com>, Arcom Control Systems

Module Parameters
-----------------

* base int
    I/O base address
* irq int
    IRQ interrupt
* clock int
    Clock rate as described in table 1 of PCA9564 datasheet

Description
-----------

This driver supports ISA boards using the Philips PCA 9564
Parallel bus to I2C bus controller
