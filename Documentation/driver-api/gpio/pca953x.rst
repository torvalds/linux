============================================
PCA953x I²C GPIO expander compatibility list
============================================

:Author: Levente Révész <levente.revesz@eilabs.com>

I went through all the datasheets and created this note listing
chip functions and register layouts.

Overview of chips
=================

Chips with the basic 4 registers
--------------------------------

These chips have 4 register banks: input, output, invert and direction.
Each of these banks contains (lines/8) registers, one for each GPIO port.

Banks offset is always a power of 2:

- 4 lines  -> bank offset is 1
- 8 lines  -> bank offset is 1
- 16 lines -> bank offset is 2
- 24 lines -> bank offset is 4
- 32 lines -> bank offset is 4
- 40 lines -> bank offset is 8

For example, register layout of GPIO expander with 24 lines:

+------+-----------------+--------+
| addr | function        | bank   |
+======+=================+========+
|  00  | input port0     |        |
+------+-----------------+        |
|  01  | input port1     | bank 0 |
+------+-----------------+        |
|  02  | input port2     |        |
+------+-----------------+--------+
|  03  | n/a             |        |
+------+-----------------+--------+
|  04  | output port0    |        |
+------+-----------------+        |
|  05  | output port1    | bank 1 |
+------+-----------------+        |
|  06  | output port2    |        |
+------+-----------------+--------+
|  07  | n/a             |        |
+------+-----------------+--------+
|  08  | invert port0    |        |
+------+-----------------+        |
|  09  | invert port1    | bank 2 |
+------+-----------------+        |
|  0A  | invert port2    |        |
+------+-----------------+--------+
|  0B  | n/a             |        |
+------+-----------------+--------+
|  0C  | direction port0 |        |
+------+-----------------+        |
|  0D  | direction port1 | bank 3 |
+------+-----------------+        |
|  0E  | direction port2 |        |
+------+-----------------+--------+
|  0F  | n/a             |        |
+------+-----------------+--------+

.. note::
     This is followed by all supported chips, except by pcal6534.

The table below shows the offsets for each of the compatible chips:

========== ===== ========= ===== ====== ====== =========
compatible lines interrupt input output invert direction
========== ===== ========= ===== ====== ====== =========
pca9536        4        no    00     01     02        03
pca9537        4       yes    00     01     02        03
pca6408        8       yes    00     01     02        03
tca6408        8       yes    00     01     02        03
pca9534        8       yes    00     01     02        03
pca9538        8       yes    00     01     02        03
pca9554        8       yes    00     01     02        03
tca9554        8       yes    00     01     02        03
pca9556        8        no    00     01     02        03
pca9557        8        no    00     01     02        03
pca6107        8       yes    00     01     02        03
pca6416       16       yes    00     02     04        06
tca6416       16       yes    00     02     04        06
pca9535       16       yes    00     02     04        06
pca9539       16       yes    00     02     04        06
tca9539       16       yes    00     02     04        06
pca9555       16       yes    00     02     04        06
max7318       16       yes    00     02     04        06
tca6424       24       yes    00     04     08        0C
========== ===== ========= ===== ====== ====== =========

Chips with additional timeout_en register
-----------------------------------------

These Maxim chips have a bus timeout function which can be enabled in
the timeout_en register. This is present in only two chips. Defaults to
timeout disabled.

========== ===== ========= ===== ====== ====== ========= ==========
compatible lines interrupt input output invert direction timeout_en
========== ===== ========= ===== ====== ====== ========= ==========
max7310        8        no    00     01     02        03         04
max7312       16       yes    00     02     04        06         08
========== ===== ========= ===== ====== ====== ========= ==========

Chips with additional int_mask register
---------------------------------------

These chips have an interrupt mask register in addition to the 4 basic
registers. The interrupt masks default to all interrupts disabled. To
use interrupts with these chips, the driver has to set the int_mask
register.

========== ===== ========= ===== ====== ====== ========= ========
compatible lines interrupt input output invert direction int_mask
========== ===== ========= ===== ====== ====== ========= ========
pca9505       40       yes    00     08     10        18       20
pca9506       40       yes    00     08     10        18       20
========== ===== ========= ===== ====== ====== ========= ========

Chips with additional int_mask and out_conf registers
-----------------------------------------------------

This chip has an interrupt mask register, and an output port
configuration register, which can select between push-pull and
open-drain modes. Each bit controls two lines. Both of these registers
are present in PCAL chips as well, albeit the out_conf works
differently.

========== ===== ========= ===== ====== ====== ========= ======== ========
compatible lines interrupt input output invert direction int_mask out_conf
========== ===== ========= ===== ====== ====== ========= ======== ========
pca9698       40       yes    00     08     10        18       20       28
========== ===== ========= ===== ====== ====== ========= ======== ========

pca9698 also has a "master output" register for setting all outputs per
port to the same value simultaneously, and a chip specific mode register
for various additional chip settings.

========== ============= ====
compatible master_output mode
========== ============= ====
pca9698               29   2A
========== ============= ====

Chips with LED blink and intensity control
------------------------------------------

These Maxim chips have no invert register.

They have two sets of output registers (output0 and output1). An internal
timer alternates the effective output between the values set in these
registers, if blink mode is enabled in the blink register. The
master_intensity register and the intensity registers together define
the PWM intensity value for each pair of outputs.

These chips can be used as simple GPIO expanders if the driver handles the
input, output0 and direction registers.

========== ===== ========= ===== ======= ========= ======= ================ ===== =========
compatible lines interrupt input output0 direction output1 master_intensity blink intensity
========== ===== ========= ===== ======= ========= ======= ================ ===== =========
max7315        8       yes    00      01        03      09               0E    0F        10
max7313       16       yes    00      02        06      0A               0E    0F        10
========== ===== ========= ===== ======= ========= ======= ================ ===== =========

Basic PCAL chips
----------------

========== ===== ========= ===== ====== ====== =========
compatible lines interrupt input output invert direction
========== ===== ========= ===== ====== ====== =========
pcal6408       8       yes    00     01     02        03
pcal9554b      8       yes    00     01     02        03
pcal6416      16       yes    00     02     04        06
pcal9535      16       yes    00     02     04        06
pcal9555a     16       yes    00     02     04        06
tcal6408       8       yes    00     01     02        03
tcal6416      16       yes    00     02     04        06
========== ===== ========= ===== ====== ====== =========

These chips have several additional features:

    1. output drive strength setting (out_strength)
    2. input latch (in_latch)
    3. pull-up/pull-down (pull_in, pull_sel)
    4. push-pull/open-drain outputs (out_conf)
    5. interrupt mask and interrupt status (int_mask, int_status)

========== ============ ======== ======= ======== ======== ========== ========
compatible out_strength in_latch pull_en pull_sel int_mask int_status out_conf
========== ============ ======== ======= ======== ======== ========== ========
pcal6408             40       42      43       44       45         46       4F
pcal9554b            40       42      43       44       45         46       4F
pcal6416             40       44      46       48       4A         4C       4F
pcal9535             40       44      46       48       4A         4C       4F
pcal9555a            40       44      46       48       4A         4C       4F
tcal6408             40       42      43       44       45         46       4F
tcal6416             40       44      46       48       4A         4C       4F
========== ============ ======== ======= ======== ======== ========== ========

Currently the driver has support for the input latch, pull-up/pull-down
and uses int_mask and int_status for interrupts.

PCAL chips with extended interrupt and output configuration functions
---------------------------------------------------------------------

========== ===== ========= ===== ====== ====== =========
compatible lines interrupt input output invert direction
========== ===== ========= ===== ====== ====== =========
pcal6524      24       yes    00     04     08        0C
pcal6534      34       yes    00     05     0A        0F
========== ===== ========= ===== ====== ====== =========

These chips have the full PCAL register set, plus the following functions:

    1. interrupt event selection: level, rising, falling, any edge
    2. clear interrupt status per line
    3. read input without clearing interrupt status
    4. individual output config (push-pull/open-drain) per output line
    5. debounce inputs

========== ============ ======== ======= ======== ======== ========== ========
compatible out_strength in_latch pull_en pull_sel int_mask int_status out_conf
========== ============ ======== ======= ======== ======== ========== ========
pcal6524             40       48      4C       50       54       58         5C
pcal6534             30       3A      3F       44       49       4E         53
========== ============ ======== ======= ======== ======== ========== ========

========== ======== ========= ============ ============== ======== ==============
compatible int_edge int_clear input_status indiv_out_conf debounce debounce_count
========== ======== ========= ============ ============== ======== ==============
pcal6524         60        68           6C             70       74             76
pcal6534         54        5E           63             68       6D             6F
========== ======== ========= ============ ============== ======== ==============

As can be seen in the table above, pcal6534 does not follow the usual
bank spacing rule. Its banks are closely packed instead.

PCA957X chips with a completely different register layout
---------------------------------------------------------

These chips have the basic 4 registers, but at unusual addresses.

Additionally, they have:

    1. pull-up/pull-down (pull_sel)
    2. a global pull enable, defaults to disabled (config)
    3. interrupt mask, interrupt status (int_mask, int_status)

========== ===== ========= ===== ====== ====== ======== ========= ====== ======== ==========
compatible lines interrupt input invert config pull_sel direction output int_mask int_status
========== ===== ========= ===== ====== ====== ======== ========= ====== ======== ==========
pca9574        8       yes    00     01     02       03        04     05       06         07
pca9575       16       yes    00     02     04       06        08     0A       0C         0E
========== ===== ========= ===== ====== ====== ======== ========= ====== ======== ==========

Currently the driver supports none of the advanced features.

XRA1202
-------

Basic 4 registers, plus advanced features:

    1. interrupt mask, defaults to interrupts disabled
    2. interrupt status
    3. interrupt event selection, level, rising, falling, any edge
       (int_mask, rising_mask, falling_mask)
    4. pull-up (no pull-down)
    5. tri-state
    6. debounce

========== ===== ========= ===== ====== ====== ========= =========
compatible lines interrupt input output invert direction pullup_en
========== ===== ========= ===== ====== ====== ========= =========
xra1202        8       yes    00     01     02        03        04
========== ===== ========= ===== ====== ====== ========= =========

========== ======== ======== ========== =========== ============ ========
compatible int_mask tristate int_status rising_mask falling_mask debounce
========== ======== ======== ========== =========== ============ ========
xra1202          05       06         07          08           09       0A
========== ======== ======== ========== =========== ============ ========

Overview of functions
=====================

This section lists chip functions that are supported by the driver
already, or are at least common in multiple chips.

Input, Output, Invert, Direction
--------------------------------

The basic 4 GPIO functions are present in all but one chip category, i.e.
`Chips with LED blink and intensity control`_ are missing the invert
register.

3 different layouts are used for these registers:

    1. banks 0, 1, 2, 3 with bank offsets of 2^n
        - all other chips

    2. banks 0, 1, 2, 3 with closely packed banks
        - pcal6534

    3. banks 0, 5, 1, 4 with bank offsets of 2^n
        - pca9574
        - pca9575

Interrupts
----------

Only an interrupt mask register
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The same layout is used for all of these:

    1. bank 5 with bank offsets of 2^n
        - pca9505
        - pca9506
        - pca9698

Interrupt mask and interrupt status registers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
These work the same way in all of the chips: mask and status have
one bit per line, 1 in the mask means interrupt enabled.

Layouts:

    1. base offset 0x40, bank 5 and bank 6, bank offsets of 2^n
        - pcal6408
        - pcal6416
        - pcal9535
        - pcal9554b
        - pcal9555a
        - pcal6524
        - tcal6408
        - tcal6416

    2. base offset 0x30, bank 5 and 6, closely packed banks
        - pcal6534

    3. bank 6 and 7, bank offsets of 2^n
        - pca9574
        - pca9575

    4. bank 5 and 7, bank offsets of 2^n
        - xra1202

Interrupt on specific edges
~~~~~~~~~~~~~~~~~~~~~~~~~~~
`PCAL chips with extended interrupt and output configuration functions`_
have an int_edge register. This contains 2 bits per line, one of 4 events
can be selected for each line:

    0: level, 1: rising edge, 2: falling edge, 3: any edge

Layouts:

    1. base offset 0x40, bank 7, bank offsets of 2^n

        - pcal6524

    2. base offset 0x30, bank 7 + offset 0x01, closely packed banks
       (out_conf is 1 byte, not (lines/8) bytes, hence the 0x01 offset)

        - pcal6534

`XRA1202`_ chips have a different mechanism for the same thing: they have
a rising mask and a falling mask, with one bit per line.

Layout:

    1. bank 5, bank offsets of 2^n

Input latch
-----------

Only `Basic PCAL chips`_ and
`PCAL chips with extended interrupt and output configuration functions`_
have this function. When the latch is enabled, the interrupt is not cleared
until the input port is read. When the latch is disabled, the interrupt
is cleared even if the input register is not read, if the input pin returns
to the logic value it had before generating the interrupt. Defaults to latch
disabled.

Currently the driver enables the latch for each line with interrupt
enabled.

An interrupt status register records which pins triggered an interrupt.
However, the status register and the input port register must be read
separately; there is no atomic mechanism to read both simultaneously, so races
are possible. Refer to the chapter `Interrupt source detection`_ to understand
the implications of this and how the driver still makes use of the latching
feature.

    1. base offset 0x40, bank 2, bank offsets of 2^n
        - pcal6408
        - pcal6416
        - pcal9535
        - pcal9554b
        - pcal9555a
        - pcal6524
        - tcal6408
        - tcal6416

    2. base offset 0x30, bank 2, closely packed banks
        - pcal6534

Pull-up and pull-down
---------------------

`Basic PCAL chips`_ and
`PCAL chips with extended interrupt and output configuration functions`_
use the same mechanism: their pull_en register enables the pull-up or pull-down
function, and their pull_sel register chooses the direction. They all use one
bit per line.

    0: pull-down, 1: pull-up

Layouts:

    1. base offset 0x40, bank 3 (en) and 4 (sel), bank offsets of 2^n
        - pcal6408
        - pcal6416
        - pcal9535
        - pcal9554b
        - pcal9555a
        - pcal6524

    2. base offset 0x30, bank 3 (en) and 4 (sel), closely packed banks
        - pcal6534

`PCA957X chips with a completely different register layout`_ have a pull_sel
register with one bit per line, and a global pull_en bit in their config
register.

Layout:

    1. bank 2 (config), bank 3 (sel), bank offsets of 2^n
        - pca9574
        - pca9575

`XRA1202`_ chips can only pull-up. They have a pullup_en register.

Layout:

    1. bank 4, bank offsets of 2^n
        - xra1202

Push-pull and open-drain
------------------------

`Chips with additional int_mask and out_conf registers`_ have this function,
but only for select IO ports. Register has 1 bit per 2 lines. In pca9698,
only port0 and port1 have this function.

    0: open-drain, 1: push-pull

Layout:

    1. base offset 5*bankoffset
        - pca9698

`Basic PCAL chips`_ have 1 bit per port in one single out_conf register.
Only whole ports can be configured.

    0: push-pull, 1: open-drain

Layout:

    1. base offset 0x4F
        - pcal6408
        - pcal6416
        - pcal9535
        - pcal9554b
        - pcal9555a
        - tcal6408
        - tcal6416

`PCAL chips with extended interrupt and output configuration functions`_
can set this for each line individually. They have the same per-port out_conf
register as `Basic PCAL chips`_, but they also have an indiv_out_conf register
with one bit per line, which inverts the effect of the port-wise setting.

    0: push-pull, 1: open-drain

Layouts:

    1. base offset 0x40 + 7*bankoffset (out_conf),
       base offset 0x60, bank 4 (indiv_out_conf) with bank offset of 2^n

        - pcal6524

    2. base offset 0x30 + 7*banksize (out_conf),
       base offset 0x54, bank 4 (indiv_out_conf), closely packed banks

        - pcal6534

This function is currently not supported by the driver.

Output drive strength
---------------------

Only PCAL chips have this function. 2 bits per line.

==== ==============
bits drive strength
==== ==============
  00          0.25x
  01          0.50x
  10          0.75x
  11          1.00x
==== ==============

    1. base offset 0x40, bank 0 and 1, bank offsets of 2^n
        - pcal6408
        - pcal6416
        - pcal9535
        - pcal9554b
        - pcal9555a
        - pcal6524
        - tcal6408
        - tcal6416

    2. base offset 0x30, bank 0 and 1, closely packed banks
        - pcal6534

Currently not supported by the driver.

Interrupt source detection
==========================

When triggered by the GPIO expander's interrupt, the driver determines which
IRQs are pending by reading the input port register.

To be able to filter on specific interrupt events for all compatible devices,
the driver keeps track of the previous input state of the lines, and emits an
IRQ only for the correct edge or level. This system works irrespective of the
number of enabled interrupts. Events will not be missed even if they occur
between the GPIO expander's interrupt and the actual I2C read. Edges could of
course be missed if the related signal level changes back to the value
previously saved by the driver before the I2C read. PCAL variants offer input
latching for that reason.

PCAL input latching
-------------------

The PCAL variants have an input latch and the driver enables this for all
interrupt-enabled lines. The interrupt is then only cleared when the input port
is read out. These variants provide an interrupt status register that records
which pins triggered an interrupt, but the status and input registers cannot be
read atomically. If another interrupt occurs on a different line after the
status register has been read but before the input port register is sampled,
that event will not be reflected in the earlier status snapshot, so relying
solely on the interrupt status register is insufficient.

Thus, the PCAL variants also have to use the existing level-change logic.

For short pulses, the first edge is captured when the input register is read,
but if the signal returns to its previous level before this read, the second
edge is not observed. As a result, successive pulses can produce identical
input values at read time and no level change is detected, causing interrupts
to be missed. Below timing diagram shows this situation where the top signal is
the input pin level and the bottom signal indicates the latched value::

  ─────┐     ┌──*───────────────┐     ┌──*─────────────────┐     ┌──*───
       │     │  .               │     │  .                 │     │  .
       │     │  │               │     │  │                 │     │  │
       └──*──┘  │               └──*──┘  │                 └──*──┘  │
  Input   │     │                  │     │                    │     │
          ▼     │                  ▼     │                    ▼     │
         IRQ    │                 IRQ    │                   IRQ    │
                .                        .                          .
  ─────┐        .┌──────────────┐        .┌────────────────┐        .┌──
       │         │              │         │                │         │
       │         │              │         │                │         │
       └────────*┘              └────────*┘                └────────*┘
  Latched       │                        │                          │
                ▼                        ▼                          ▼
              READ 0                   READ 0                     READ 0
                                     NO CHANGE                  NO CHANGE

To deal with this, events indicated by the interrupt status register are merged
with events detected through the existing level-change logic. As a result:

- short pulses, whose second edges are invisible, are detected via the
  interrupt status register, and
- interrupts that occur between the status and input reads are still
  caught by the generic level-change logic.

Note that this is still best-effort: the status and input registers are read
separately, and short pulses on other lines may occur in between those reads.
Such pulses can still be latched as an interrupt without leaving an observable
level change at read time, and may not be attributable to a specific edge. This
does not reduce detection compared to the generic path, but reflects inherent
atomicity limitations.

Datasheets
==========

- MAX7310: https://datasheets.maximintegrated.com/en/ds/MAX7310.pdf
- MAX7312: https://datasheets.maximintegrated.com/en/ds/MAX7312.pdf
- MAX7313: https://datasheets.maximintegrated.com/en/ds/MAX7313.pdf
- MAX7315: https://datasheets.maximintegrated.com/en/ds/MAX7315.pdf
- MAX7318: https://datasheets.maximintegrated.com/en/ds/MAX7318.pdf
- PCA6107: https://pdf1.alldatasheet.com/datasheet-pdf/view/161780/TI/PCA6107.html
- PCA6408A: https://www.nxp.com/docs/en/data-sheet/PCA6408A.pdf
- PCA6416A: https://www.nxp.com/docs/en/data-sheet/PCA6416A.pdf
- PCA9505: https://www.nxp.com/docs/en/data-sheet/PCA9505_9506.pdf
- PCA9505: https://www.nxp.com/docs/en/data-sheet/PCA9505_9506.pdf
- PCA9534: https://www.nxp.com/docs/en/data-sheet/PCA9534.pdf
- PCA9535: https://www.nxp.com/docs/en/data-sheet/PCA9535_PCA9535C.pdf
- PCA9536: https://www.nxp.com/docs/en/data-sheet/PCA9536.pdf
- PCA9537: https://www.nxp.com/docs/en/data-sheet/PCA9537.pdf
- PCA9538: https://www.nxp.com/docs/en/data-sheet/PCA9538.pdf
- PCA9539: https://www.nxp.com/docs/en/data-sheet/PCA9539_PCA9539R.pdf
- PCA9554: https://www.nxp.com/docs/en/data-sheet/PCA9554_9554A.pdf
- PCA9555: https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf
- PCA9556: https://www.nxp.com/docs/en/data-sheet/PCA9556.pdf
- PCA9557: https://www.nxp.com/docs/en/data-sheet/PCA9557.pdf
- PCA9574: https://www.nxp.com/docs/en/data-sheet/PCA9574.pdf
- PCA9575: https://www.nxp.com/docs/en/data-sheet/PCA9575.pdf
- PCA9698: https://www.nxp.com/docs/en/data-sheet/PCA9698.pdf
- PCAL6408A: https://www.nxp.com/docs/en/data-sheet/PCAL6408A.pdf
- PCAL6416A: https://www.nxp.com/docs/en/data-sheet/PCAL6416A.pdf
- PCAL6524: https://www.nxp.com/docs/en/data-sheet/PCAL6524.pdf
- PCAL6534: https://www.nxp.com/docs/en/data-sheet/PCAL6534.pdf
- PCAL9535A: https://www.nxp.com/docs/en/data-sheet/PCAL9535A.pdf
- PCAL9554B: https://www.nxp.com/docs/en/data-sheet/PCAL9554B_PCAL9554C.pdf
- PCAL9555A: https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf
- TCA6408A: https://www.ti.com/lit/gpn/tca6408a
- TCA6416: https://www.ti.com/lit/gpn/tca6416
- TCA6424: https://www.ti.com/lit/gpn/tca6424
- TCA9539: https://www.ti.com/lit/gpn/tca9539
- TCA9554: https://www.ti.com/lit/gpn/tca9554
- XRA1202: https://assets.maxlinear.com/web/documents/xra1202_1202p_101_042213.pdf
