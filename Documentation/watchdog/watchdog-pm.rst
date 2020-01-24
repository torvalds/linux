===============================================
The Linux WatchDog Timer Power Management Guide
===============================================

Last reviewed: 17-Dec-2018

Wolfram Sang <wsa+renesas@sang-engineering.com>

Introduction
------------
This document states rules about watchdog devices and their power management
handling to ensure a uniform behaviour for Linux systems.


Ping on resume
--------------
On resume, a watchdog timer shall be reset to its selected value to give
userspace enough time to resume. [1] [2]

[1] https://patchwork.kernel.org/patch/10252209/

[2] https://patchwork.kernel.org/patch/10711625/
