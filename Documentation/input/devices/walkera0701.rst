===========================
Walkera WK-0701 transmitter
===========================

Walkera WK-0701 transmitter is supplied with a ready to fly Walkera
helicopters such as HM36, HM37, HM60. The walkera0701 module enables to use
this transmitter as joystick

Devel homepage and download:
http://zub.fei.tuke.sk/walkera-wk0701/

or use cogito:
cg-clone http://zub.fei.tuke.sk/GIT/walkera0701-joystick


Connecting to PC
================

At back side of transmitter S-video connector can be found. Modulation
pulses from processor to HF part can be found at pin 2 of this connector,
pin 3 is GND. Between pin 3 and CPU 5k6 resistor can be found. To get
modulation pulses to PC, signal pulses must be amplified.

Cable: (walkera TX to parport)

Walkera WK-0701 TX S-VIDEO connector::

 (back side of TX)
     __   __              S-video:                                  canon25
    /  |_|  \             pin 2 (signal)              NPN           parport
   / O 4 3 O \            pin 3 (GND)        LED        ________________  10 ACK
  ( O 2   1 O )                                         | C
   \   ___   /      2 ________________________|\|_____|/
    | [___] |                                 |/|   B |\
     -------        3 __________________________________|________________ 25 GND
                                                          E

I use green LED and BC109 NPN transistor.

Software
========

Build kernel with walkera0701 module. Module walkera0701 need exclusive
access to parport, modules like lp must be unloaded before loading
walkera0701 module, check dmesg for error messages. Connect TX to PC by
cable and run jstest /dev/input/js0 to see values from TX. If no value can
be changed by TX "joystick", check output from /proc/interrupts. Value for
(usually irq7) parport must increase if TX is on.



Technical details
=================

Driver use interrupt from parport ACK input bit to measure pulse length
using hrtimers.

Frame format:
Based on walkera WK-0701 PCM Format description by Shaul Eizikovich.
(downloaded from http://www.smartpropoplus.com/Docs/Walkera_Wk-0701_PCM.pdf)

Signal pulses
-------------

::

                     (ANALOG)
      SYNC      BIN   OCT
    +---------+      +------+
    |         |      |      |
  --+         +------+      +---

Frame
-----

::

 SYNC , BIN1, OCT1, BIN2, OCT2 ... BIN24, OCT24, BIN25, next frame SYNC ..

pulse length
------------

::

   Binary values:		Analog octal values:

   288 uS Binary 0		318 uS       000
   438 uS Binary 1		398 uS       001
				478 uS       010
				558 uS       011
				638 uS       100
  1306 uS SYNC			718 uS       101
				798 uS       110
				878 uS       111

24 bin+oct values + 1 bin value = 24*4+1 bits  = 97 bits

(Warning, pulses on ACK are inverted by transistor, irq is raised up on sync
to bin change or octal value to bin change).

Binary data representations
---------------------------

One binary and octal value can be grouped to nibble. 24 nibbles + one binary
values can be sampled between sync pulses.

Values for first four channels (analog joystick values) can be found in
first 10 nibbles. Analog value is represented by one sign bit and 9 bit
absolute binary value. (10 bits per channel). Next nibble is checksum for
first ten nibbles.

Next nibbles 12 .. 21 represents four channels (not all channels can be
directly controlled from TX). Binary representations are the same as in first
four channels. In nibbles 22 and 23 is a special magic number. Nibble 24 is
checksum for nibbles 12..23.

After last octal value for nibble 24 and next sync pulse one additional
binary value can be sampled. This bit and magic number is not used in
software driver. Some details about this magic numbers can be found in
Walkera_Wk-0701_PCM.pdf.

Checksum calculation
--------------------

Summary of octal values in nibbles must be same as octal value in checksum
nibble (only first 3 bits are used). Binary value for checksum nibble is
calculated by sum of binary values in checked nibbles + sum of octal values
in checked nibbles divided by 8. Only bit 0 of this sum is used.
