.. SPDX-License-Identifier: GPL-2.0 OR GFDL-1.2-no-invariants-or-later

=================
EDAC/RAS features
=================

Copyright (c) 2024-2025 HiSilicon Limited.

:Author:   Shiju Jose <shiju.jose@huawei.com>
:License:  The GNU Free Documentation License, Version 1.2 without
           Invariant Sections, Front-Cover Texts nor Back-Cover Texts.
           (dual licensed under the GPL v2)

- Written for: 6.15

Introduction
------------

EDAC/RAS components plugging and high-level design:

1. Scrub control

2. Error Check Scrub (ECS) control

3. ACPI RAS2 features

4. Post Package Repair (PPR) control

5. Memory Sparing Repair control

High level design is illustrated in the following diagram::

        +-----------------------------------------------+
        |   Userspace - Rasdaemon                       |
        | +-------------+                               |
        | | RAS CXL mem |     +---------------+         |
        | |error handler|---->|               |         |
        | +-------------+     | RAS dynamic   |         |
        | +-------------+     | scrub, memory |         |
        | | RAS memory  |---->| repair control|         |
        | |error handler|     +----|----------+         |
        | +-------------+          |                    |
        +--------------------------|--------------------+
                                   |
                                   |
   +-------------------------------|------------------------------+
   |     Kernel EDAC extension for | controlling RAS Features     |
   |+------------------------------|----------------------------+ |
   || EDAC Core          Sysfs EDAC| Bus                        | |
   ||   +--------------------------|---------------------------+| |
   ||   |/sys/bus/edac/devices/<dev>/scrubX/ |   | EDAC device || |
   ||   |/sys/bus/edac/devices/<dev>/ecsX/   |<->| EDAC MC     || |
   ||   |/sys/bus/edac/devices/<dev>/repairX |   | EDAC sysfs  || |
   ||   +---------------------------|--------------------------+| |
   ||                           EDAC|Bus                        | |
   ||                               |                           | |
   ||   +----------+ Get feature    |      Get feature          | |
   ||   |          | desc +---------|------+ desc +----------+  | |
   ||   |EDAC scrub|<-----| EDAC device    |      |          |  | |
   ||   +----------+      | driver- RAS    |----->| EDAC mem |  | |
   ||   +----------+      | feature control|      | repair   |  | |
   ||   |          |<-----|                |      +----------+  | |
   ||   |EDAC ECS  |      +---------|------+                    | |
   ||   +----------+    Register RAS|features                   | |
   ||         ______________________|_____________              | |
   |+---------|---------------|------------------|--------------+ |
   |  +-------|----+  +-------|-------+     +----|----------+     |
   |  |            |  | CXL mem driver|     | Client driver |     |
   |  | ACPI RAS2  |  | scrub, ECS,   |     | memory repair |     |
   |  | driver     |  | sparing, PPR  |     | features      |     |
   |  +-----|------+  +-------|-------+     +------|--------+     |
   |        |                 |                    |              |
   +--------|-----------------|--------------------|--------------+
            |                 |                    |
   +--------|-----------------|--------------------|--------------+
   |    +---|-----------------|--------------------|-------+      |
   |    |                                                  |      |
   |    |            Platform HW and Firmware              |      |
   |    +--------------------------------------------------+      |
   +--------------------------------------------------------------+


1. EDAC Features components - Create feature-specific descriptors. For
   example: scrub, ECS, memory repair in the above diagram.

2. EDAC device driver for controlling RAS Features - Get feature's attribute
   descriptors from EDAC RAS feature component and registers device's RAS
   features with EDAC bus and expose the features control attributes via
   sysfs. For example, /sys/bus/edac/devices/<dev-name>/<feature>X/

3. RAS dynamic feature controller - Userspace sample modules in rasdaemon for
   dynamic scrub/repair control to issue scrubbing/repair when excess number
   of corrected memory errors are reported in a short span of time.

RAS features
------------
1. Memory Scrub

Memory scrub features are documented in `Documentation/edac/scrub.rst`.

2. Memory Repair

Memory repair features are documented in `Documentation/edac/memory_repair.rst`.
