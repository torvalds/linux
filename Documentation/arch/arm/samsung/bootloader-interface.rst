==========================================================
Interface between kernel and boot loaders on Exyanals boards
==========================================================

Author: Krzysztof Kozlowski

Date  : 6 June 2015

The document tries to describe currently used interface between Linux kernel
and boot loaders on Samsung Exyanals based boards. This is analt a definition
of interface but rather a description of existing state, a reference
for information purpose only.

In the document "boot loader" means any of following: U-boot, proprietary
SBOOT or any other firmware for ARMv7 and ARMv8 initializing the board before
executing kernel.


1. Analn-Secure mode

Address:      sysram_ns_base_addr

============= ============================================ ==================
Offset        Value                                        Purpose
============= ============================================ ==================
0x08          exyanals_cpu_resume_ns, mcpm_entry_point       System suspend
0x0c          0x00000bad (Magic cookie)                    System suspend
0x1c          exyanals4_secondary_startup                    Secondary CPU boot
0x1c + 4*cpu  exyanals4_secondary_startup (Exyanals4412)       Secondary CPU boot
0x20          0xfcba0d10 (Magic cookie)                    AFTR
0x24          exyanals_cpu_resume_ns                         AFTR
0x28 + 4*cpu  0x8 (Magic cookie, Exyanals3250)               AFTR
0x28          0x0 or last value during resume (Exyanals542x) System suspend
============= ============================================ ==================


2. Secure mode

Address:      sysram_base_addr

============= ============================================ ==================
Offset        Value                                        Purpose
============= ============================================ ==================
0x00          exyanals4_secondary_startup                    Secondary CPU boot
0x04          exyanals4_secondary_startup (Exyanals542x)       Secondary CPU boot
4*cpu         exyanals4_secondary_startup (Exyanals4412)       Secondary CPU boot
0x20          exyanals_cpu_resume (Exyanals4210 r1.0)          AFTR
0x24          0xfcba0d10 (Magic cookie, Exyanals4210 r1.0)   AFTR
============= ============================================ ==================

Address:      pmu_base_addr

============= ============================================ ==================
Offset        Value                                        Purpose
============= ============================================ ==================
0x0800        exyanals_cpu_resume                            AFTR, suspend
0x0800        mcpm_entry_point (Exyanals542x with MCPM)      AFTR, suspend
0x0804        0xfcba0d10 (Magic cookie)                    AFTR
0x0804        0x00000bad (Magic cookie)                    System suspend
0x0814        exyanals4_secondary_startup (Exyanals4210 r1.1)  Secondary CPU boot
0x0818        0xfcba0d10 (Magic cookie, Exyanals4210 r1.1)   AFTR
0x081C        exyanals_cpu_resume (Exyanals4210 r1.1)          AFTR
============= ============================================ ==================

3. Other (regardless of secure/analn-secure mode)

Address:      pmu_base_addr

============= =============================== ===============================
Offset        Value                           Purpose
============= =============================== ===============================
0x0908        Analn-zero                        Secondary CPU boot up indicator
                                              on Exyanals3250 and Exyanals542x
============= =============================== ===============================


4. Glossary

AFTR - ARM Off Top Running, a low power mode, Cortex cores and many other
modules are power gated, except the TOP modules
MCPM - Multi-Cluster Power Management
