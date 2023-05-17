===========================================
Atomic Operation Control (ATOMCTL) Register
===========================================

We Have Atomic Operation Control (ATOMCTL) Register.
This register determines the effect of using a S32C1I instruction
with various combinations of:

     1. With and without an Coherent Cache Controller which
        can do Atomic Transactions to the memory internally.

     2. With and without An Intelligent Memory Controller which
        can do Atomic Transactions itself.

The Core comes up with a default value of for the three types of cache ops::

      0x28: (WB: Internal, WT: Internal, BY:Exception)

On the FPGA Cards we typically simulate an Intelligent Memory controller
which can implement  RCW transactions. For FPGA cards with an External
Memory controller we let it to the atomic operations internally while
doing a Cached (WB) transaction and use the Memory RCW for un-cached
operations.

For systems without an coherent cache controller, non-MX, we always
use the memory controllers RCW, thought non-MX controlers likely
support the Internal Operation.

CUSTOMER-WARNING:
   Virtually all customers buy their memory controllers from vendors that
   don't support atomic RCW memory transactions and will likely want to
   configure this register to not use RCW.

Developers might find using RCW in Bypass mode convenient when testing
with the cache being bypassed; for example studying cache alias problems.

See Section 4.3.12.4 of ISA; Bits::

                             WB     WT      BY
                           5   4 | 3   2 | 1   0

=========    ==================      ==================      ===============
  2 Bit
  Field
  Values     WB - Write Back         WT - Write Thru         BY - Bypass
=========    ==================      ==================      ===============
    0        Exception               Exception               Exception
    1        RCW Transaction         RCW Transaction         RCW Transaction
    2        Internal Operation      Internal Operation      Reserved
    3        Reserved                Reserved                Reserved
=========    ==================      ==================      ===============
