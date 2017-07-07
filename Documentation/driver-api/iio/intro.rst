.. include:: <isonum.txt>

============
Introduction
============

The main purpose of the Industrial I/O subsystem (IIO) is to provide support
for devices that in some sense perform either
analog-to-digital conversion (ADC) or digital-to-analog conversion (DAC)
or both. The aim is to fill the gap between the somewhat similar hwmon and
:doc:`input <../input>` subsystems. Hwmon is directed at low sample rate
sensors used to monitor and control the system itself, like fan speed control
or temperature measurement. :doc:`Input <../input>` is, as its name suggests,
focused on human interaction input devices (keyboard, mouse, touchscreen).
In some cases there is considerable overlap between these and IIO.

Devices that fall into this category include:

* analog to digital converters (ADCs)
* accelerometers
* capacitance to digital converters (CDCs)
* digital to analog converters (DACs)
* gyroscopes
* inertial measurement units (IMUs)
* color and light sensors
* magnetometers
* pressure sensors
* proximity sensors
* temperature sensors

Usually these sensors are connected via :doc:`SPI <../spi>` or
:doc:`I2C <../i2c>`. A common use case of the sensors devices is to have
combined functionality (e.g. light plus proximity sensor).
