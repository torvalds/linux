==========================================================
Interface between kernel and boot loaders on Exyyess boards
==========================================================

Author: Krzysztof Kozlowski

Date  : 6 June 2015

The document tries to describe currently used interface between Linux kernel
and boot loaders on Samsung Exyyess based boards. This is yest a definition
of interface but rather a description of existing state, a reference
for information purpose only.

In the document "boot loader" means any of following: U-boot, proprietary
SBOOT or any other firmware for ARMv7 and ARMv8 initializing the board before
executing kernel.


1. Non-Secure mode

Address:      sysram_ns_base_addr

============= ============================================ ==================
Offset        Value                                        Purpose
============= ============================================ ==================
0x08          exyyess_cpu_resume_ns, mcpm_entry_point       System suspend
0x0c          0x00000bad (Magic cookie)                    System suspend
0x1c          exyyess4_secondary_startup                    Secondary CPU boot
0x1c + 4*cpu  exyyess4_secondary_startup (Exyyess4412)       Secondary CPU boot
0x20          0xfcba0d10 (Magic cookie)                    AFTR
0x24          exyyess_cpu_resume_ns                         AFTR
0x28 + 4*cpu  0x8 (Magic cookie, Exyyess3250)               AFTR
0x28          0x0 or last value during resume (Exyyess542x) System suspend
============= ============================================ ==================


2. Secure mode

Address:      sysram_base_addr

============= ============================================ ==================
Offset        Value                                        Purpose
============= ============================================ ==================
0x00          exyyess4_secondary_startup                    Secondary CPU boot
0x04          exyyess4_secondary_startup (Exyyess542x)       Secondary CPU boot
4*cpu         exyyess4_secondary_startup (Exyyess4412)       Secondary CPU boot
0x20          exyyess_cpu_resume (Exyyess4210 r1.0)          AFTR
0x24          0xfcba0d10 (Magic cookie, Exyyess4210 r1.0)   AFTR
============= ============================================ ==================

Address:      pmu_base_addr

============= ============================================ ==================
Offset        Value                                        Purpose
============= ============================================ ==================
0x0800        exyyess_cpu_resume                            AFTR, suspend
0x0800        mcpm_entry_point (Exyyess542x with MCPM)      AFTR, suspend
0x0804        0xfcba0d10 (Magic cookie)                    AFTR
0x0804        0x00000bad (Magic cookie)                    System suspend
0x0814        exyyess4_secondary_startup (Exyyess4210 r1.1)  Secondary CPU boot
0x0818        0xfcba0d10 (Magic cookie, Exyyess4210 r1.1)   AFTR
0x081C        exyyess_cpu_resume (Exyyess4210 r1.1)          AFTR
============= ============================================ ==================

3. Other (regardless of secure/yesn-secure mode)

Address:      pmu_base_addr

============= =============================== ===============================
Offset        Value                           Purpose
============= =============================== ===============================
0x0908        Non-zero                        Secondary CPU boot up indicator
                                              on Exyyess3250 and Exyyess542x
============= =============================== ===============================


4. Glossary

AFTR - ARM Off Top Running, a low power mode, Cortex cores and many other
modules are power gated, except the TOP modules
MCPM - Multi-Cluster Power Management
