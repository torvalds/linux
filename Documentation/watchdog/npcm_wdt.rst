.. SPDX-License-Identifier: GPL-2.0

=============
NPCM Watchdog
=============

The NPCM watchdog driver can report reset-cause information on
``nuvoton,npcm750-wdt`` and ``nuvoton,npcm845-wdt`` systems.

Userspace can read the latched reset cause through
``WDIOC_GETBOOTSTATUS``. When ``CONFIG_WATCHDOG_SYSFS`` is enabled, the
same value is also visible through ``/sys/class/watchdog/watchdogN/bootstatus``.

The mapping is fixed in the driver. It exposes the SoC reset indications
through the generic watchdog bootstatus flags and is not configurable from
Device Tree.

.. list-table:: Reset-cause mapping
   :header-rows: 1

   * - Platform
     - Reset indication
     - Bootstatus flag
     - Reported meaning
   * - NPCM750 and NPCM845
     - ``PORST``
     - ``WDIOF_OVERHEAT``
     - power-on reset
   * - NPCM750 and NPCM845
     - ``CORST``
     - ``WDIOF_FANFAULT``
     - core reset
   * - NPCM750 and NPCM845
     - ``SWR1RST``
     - ``WDIOF_EXTERN1``
     - software reset source 1
   * - NPCM750 and NPCM845
     - ``SWR2RST``
     - ``WDIOF_EXTERN2``
     - software reset source 2
   * - NPCM750 and NPCM845
     - ``SWR3RST``
     - ``WDIOF_POWERUNDER``
     - software reset source 3
   * - NPCM750
     - ``SWR4RST``
     - ``WDIOF_POWEROVER``
     - software reset source 4
   * - NPCM845
     - ``TIP reset`` (``INTCR2[25]``)
     - ``WDIOF_POWEROVER``
     - TIP reset

``WDIOF_CARDRESET`` is reported only for the watchdog instance whose own
reset-status bit is latched. On systems with three watchdog instances, this
maps ``WD0RST``, ``WD1RST``, and ``WD2RST`` to ``watchdog0``, ``watchdog1``,
and ``watchdog2`` respectively.

The driver may report ``WDIOF_CARDRESET`` together with one or more of the
reset-cause flags listed above.

On NPCM750, the driver samples ``RESSR``. When reset bits are still latched,
it clears them and stores the sampled value in ``SCRPAD2`` so later watchdog
probes can report the same boot-time state.

On NPCM845, the driver samples ``INTCR2``. When reset bits are still latched,
it clears them and stores the sampled value in ``SCRPAD10`` so later watchdog
probes can report the same boot-time state.

The WPCM450 watchdog continues to operate without this reset-indication
mapping.
