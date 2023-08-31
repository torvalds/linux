Software emulation of deprecated SWP instruction (CONFIG_SWP_EMULATE)
---------------------------------------------------------------------

ARMv6 architecture deprecates use of the SWP/SWPB instructions, and recommends
moving to the load-locked/store-conditional instructions LDREX and STREX.

ARMv7 multiprocessing extensions introduce the ability to disable these
instructions, triggering an undefined instruction exception when executed.
Trapped instructions are emulated using an LDREX/STREX or LDREXB/STREXB
sequence. If a memory access fault (an abort) occurs, a segmentation fault is
signalled to the triggering process.

/proc/cpu/swp_emulation holds some statistics/information, including the PID of
the last process to trigger the emulation to be invocated. For example::

  Emulated SWP:		12
  Emulated SWPB:		0
  Aborted SWP{B}:		1
  Last process:		314


NOTE:
  when accessing uncached shared regions, LDREX/STREX rely on an external
  transaction monitoring block called a global monitor to maintain update
  atomicity. If your system does not implement a global monitor, this option can
  cause programs that perform SWP operations to uncached memory to deadlock, as
  the STREX operation will always fail.
