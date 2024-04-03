.. SPDX-License-Identifier: GPL-2.0

=======================
I2C Address Translators
=======================

Author: Luca Ceresoli <luca@lucaceresoli.net>
Author: Tomi Valkeinen <tomi.valkeinen@ideasonboard.com>

Description
-----------

An I2C Address Translator (ATR) is a device with an I2C slave parent
("upstream") port and N I2C master child ("downstream") ports, and
forwards transactions from upstream to the appropriate downstream port
with a modified slave address. The address used on the parent bus is
called the "alias" and is (potentially) different from the physical
slave address of the child bus. Address translation is done by the
hardware.

An ATR looks similar to an i2c-mux except:
 - the address on the parent and child busses can be different
 - there is normally no need to select the child port; the alias used on the
   parent bus implies it

The ATR functionality can be provided by a chip with many other features.
The kernel i2c-atr provides a helper to implement an ATR within a driver.

The ATR creates a new I2C "child" adapter on each child bus. Adding
devices on the child bus ends up in invoking the driver code to select
an available alias. Maintaining an appropriate pool of available aliases
and picking one for each new device is up to the driver implementer. The
ATR maintains a table of currently assigned alias and uses it to modify
all I2C transactions directed to devices on the child buses.

A typical example follows.

Topology::

                      Slave X @ 0x10
              .-----.   |
  .-----.     |     |---+---- B
  | CPU |--A--| ATR |
  `-----'     |     |---+---- C
              `-----'   |
                      Slave Y @ 0x10

Alias table:

A, B and C are three physical I2C busses, electrically independent from
each other. The ATR receives the transactions initiated on bus A and
propagates them on bus B or bus C or none depending on the device address
in the transaction and based on the alias table.

Alias table:

.. table::

   ===============   =====
   Client            Alias
   ===============   =====
   X (bus B, 0x10)   0x20
   Y (bus C, 0x10)   0x30
   ===============   =====

Transaction:

 - Slave X driver requests a transaction (on adapter B), slave address 0x10
 - ATR driver finds slave X is on bus B and has alias 0x20, rewrites
   messages with address 0x20, forwards to adapter A
 - Physical I2C transaction on bus A, slave address 0x20
 - ATR chip detects transaction on address 0x20, finds it in table,
   propagates transaction on bus B with address translated to 0x10,
   keeps clock stretched on bus A waiting for reply
 - Slave X chip (on bus B) detects transaction at its own physical
   address 0x10 and replies normally
 - ATR chip stops clock stretching and forwards reply on bus A,
   with address translated back to 0x20
 - ATR driver receives the reply, rewrites messages with address 0x10
   as they were initially
 - Slave X driver gets back the msgs[], with reply and address 0x10

Usage:

 1. In the driver (typically in the probe function) add an ATR by
    calling i2c_atr_new() passing attach/detach callbacks
 2. When the attach callback is called pick an appropriate alias,
    configure it in the chip and return the chosen alias in the
    alias_id parameter
 3. When the detach callback is called, deconfigure the alias from
    the chip and put the alias back in the pool for later usage

I2C ATR functions and data structures
-------------------------------------

.. kernel-doc:: include/linux/i2c-atr.h
