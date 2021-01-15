===========================
Samsung GPIO implementation
===========================

Introduction
------------

This outlines the Samsung GPIO implementation and the architecture
specific calls provided alongside the drivers/gpio core.


S3C24XX (Legacy)
----------------

See Documentation/arm/samsung-s3c24xx/gpio.rst for more information
about these devices. Their implementation has been brought into line
with the core samsung implementation described in this document.


GPIOLIB integration
-------------------

The gpio implementation uses gpiolib as much as possible, only providing
specific calls for the items that require Samsung specific handling, such
as pin special-function or pull resistor control.

GPIO numbering is synchronised between the Samsung and gpiolib system.


PIN configuration
-----------------

Pin configuration is specific to the Samsung architecture, with each SoC
registering the necessary information for the core gpio configuration
implementation to configure pins as necessary.

The s3c_gpio_cfgpin() and s3c_gpio_setpull() provide the means for a
driver or machine to change gpio configuration.

See arch/arm/mach-s3c/gpio-cfg.h for more information on these functions.
