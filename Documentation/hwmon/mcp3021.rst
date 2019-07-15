Kernel driver MCP3021
=====================

Supported chips:

  * Microchip Technology MCP3021

    Prefix: 'mcp3021'

    Datasheet: http://ww1.microchip.com/downloads/en/DeviceDoc/21805a.pdf

  * Microchip Technology MCP3221

    Prefix: 'mcp3221'

    Datasheet: http://ww1.microchip.com/downloads/en/DeviceDoc/21732c.pdf



Authors:

   - Mingkai Hu
   - Sven Schuchmann <schuchmann@schleissheimer.de>

Description
-----------

This driver implements support for the Microchip Technology MCP3021 and
MCP3221 chip.

The Microchip Technology Inc. MCP3021 is a successive approximation A/D
converter (ADC) with 10-bit resolution. The MCP3221 has 12-bit resolution.

These devices provide one single-ended input with very low power consumption.
Communication to the MCP3021/MCP3221  is performed using a 2-wire I2C
compatible interface. Standard (100 kHz) and Fast (400 kHz) I2C modes are
available. The default I2C device address is 0x4d (contact the Microchip
factory for additional address options).
