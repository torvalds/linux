.. SPDX-License-Identifier: GPL-2.0

The Rockchip Image Signal Processor Driver (rkisp1)
===================================================

Versions and their differences
------------------------------

The rkisp1 block underwent some changes between SoC implementations.
The vendor designates them as:

- V10: used at least in rk3288 and rk3399
- V11: declared in the original vendor code, but not used
- V12: used at least in rk3326 and px30
- V13: used at least in rk1808
- V20: used in rk3568 and beyond

Right now the kernel supports rkisp1 implementations based
on V10 and V12 variants. V11 does not seem to be actually used
and V13 will need some more additions but isn't researched yet,
especially as it seems to be limited to the rk1808 which hasn't
reached much market spread.

V20 on the other hand will probably be used in future SoCs and
has seen really big changes in the vendor kernel, so will need
quite a bit of research.

Changes from V10 to V12
-----------------------

- V12 supports a new CSI-host implementation but can still
  also use the same implementation from V10
- The module for lens shading correction got changed
  from 12bit to 13bit width
- The AWB and AEC modules got replaced to support finer
  grained data collection

Changes from V12 to V13
-----------------------

The list for V13 is incomplete and needs further investigation.

- V13 does not support the old CSI-host implementation anymore
