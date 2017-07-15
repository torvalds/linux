===================================================================
A driver for a selfmade cheap BT8xx based PCI GPIO-card (bt8xxgpio)
===================================================================

For advanced documentation, see http://www.bu3sch.de/btgpio.php

A generic digital 24-port PCI GPIO card can be built out of an ordinary
Brooktree bt848, bt849, bt878 or bt879 based analog TV tuner card. The
Brooktree chip is used in old analog Hauppauge WinTV PCI cards. You can easily
find them used for low prices on the net.

The bt8xx chip does have 24 digital GPIO ports.
These ports are accessible via 24 pins on the SMD chip package.


How to physically access the GPIO pins
======================================

The are several ways to access these pins. One might unsolder the whole chip
and put it on a custom PCI board, or one might only unsolder each individual
GPIO pin and solder that to some tiny wire. As the chip package really is tiny
there are some advanced soldering skills needed in any case.

The physical pinouts are drawn in the following ASCII art.
The GPIO pins are marked with G00-G23::

                                           G G G G G G G G G G G G     G G G G G G
                                           0 0 0 0 0 0 0 0 0 0 1 1     1 1 1 1 1 1
                                           0 1 2 3 4 5 6 7 8 9 0 1     2 3 4 5 6 7
           | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | |
           ---------------------------------------------------------------------------
         --|                               ^                                     ^   |--
         --|                               pin 86                           pin 67   |--
         --|                                                                         |--
         --|                                                               pin 61 >  |-- G18
         --|                                                                         |-- G19
         --|                                                                         |-- G20
         --|                                                                         |-- G21
         --|                                                                         |-- G22
         --|                                                               pin 56 >  |-- G23
         --|                                                                         |--
         --|                           Brooktree 878/879                             |--
         --|                                                                         |--
         --|                                                                         |--
         --|                                                                         |--
         --|                                                                         |--
         --|                                                                         |--
         --|                                                                         |--
         --|                                                                         |--
         --|                                                                         |--
         --|                                                                         |--
         --|                                                                         |--
         --|                                                                         |--
         --|                                                                         |--
         --|                                                                         |--
         --|   O                                                                     |--
         --|                                                                         |--
           ---------------------------------------------------------------------------
           | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | |
           ^
           This is pin 1

