===================================
cfag12864b LCD Driver Documentation
===================================

:License:		GPLv2
:Author & Maintainer:	Miguel Ojeda <ojeda@kernel.org>
:Date:			2006-10-27



.. INDEX

	1. DRIVER INFORMATION
	2. DEVICE INFORMATION
	3. WIRING
	4. USERSPACE PROGRAMMING

1. Driver Information
---------------------

This driver supports a cfag12864b LCD.


2. Device Information
---------------------

:Manufacturer:	Crystalfontz
:Device Name:	Crystalfontz 12864b LCD Series
:Device Code:	cfag12864b
:Webpage:	http://www.crystalfontz.com
:Device Webpage: http://www.crystalfontz.com/products/12864b/
:Type:		LCD (Liquid Crystal Display)
:Width:		128
:Height:	64
:Colors:	2 (B/N)
:Controller:	ks0108
:Controllers:	2
:Pages:		8 each controller
:Addresses:	64 each page
:Data size:	1 byte each address
:Memory size:	2 * 8 * 64 * 1 = 1024 bytes = 1 Kbyte


3. Wiring
---------

The cfag12864b LCD Series don't have official wiring.

The common wiring is done to the parallel port as shown::

  Parallel Port                          cfag12864b

    Name Pin#                            Pin# Name

  Strobe ( 1)------------------------------(17) Enable
  Data 0 ( 2)------------------------------( 4) Data 0
  Data 1 ( 3)------------------------------( 5) Data 1
  Data 2 ( 4)------------------------------( 6) Data 2
  Data 3 ( 5)------------------------------( 7) Data 3
  Data 4 ( 6)------------------------------( 8) Data 4
  Data 5 ( 7)------------------------------( 9) Data 5
  Data 6 ( 8)------------------------------(10) Data 6
  Data 7 ( 9)------------------------------(11) Data 7
         (10)                      [+5v]---( 1) Vdd
         (11)                      [GND]---( 2) Ground
         (12)                      [+5v]---(14) Reset
         (13)                      [GND]---(15) Read / Write
    Line (14)------------------------------(13) Controller Select 1
         (15)
    Init (16)------------------------------(12) Controller Select 2
  Select (17)------------------------------(16) Data / Instruction
  Ground (18)---[GND]              [+5v]---(19) LED +
  Ground (19)---[GND]
  Ground (20)---[GND]              E    A             Values:
  Ground (21)---[GND]       [GND]---[P1]---(18) Vee    - R = Resistor = 22 ohm
  Ground (22)---[GND]                |                 - P1 = Preset = 10 Kohm
  Ground (23)---[GND]       ----   S ------( 3) V0     - P2 = Preset = 1 Kohm
  Ground (24)---[GND]       |  |
  Ground (25)---[GND] [GND]---[P2]---[R]---(20) LED -


4. Userspace Programming
------------------------

The cfag12864bfb describes a framebuffer device (/dev/fbX).

It has a size of 1024 bytes = 1 Kbyte.
Each bit represents one pixel. If the bit is high, the pixel will
turn on. If the pixel is low, the pixel will turn off.

You can use the framebuffer as a file: fopen, fwrite, fclose...
Although the LCD won't get updated until the next refresh time arrives.

Also, you can mmap the framebuffer: open & mmap, munmap & close...
which is the best option for most uses.

Check samples/auxdisplay/cfag12864b-example.c
for a real working userspace complete program with usage examples.
