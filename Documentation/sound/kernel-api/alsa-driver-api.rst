===================
The ALSA Driver API
===================

Management of Cards and Devices
===============================

Card Management
---------------
.. kernel-doc:: sound/core/init.c

Device Components
-----------------
.. kernel-doc:: sound/core/device.c

Module requests and Device File Entries
---------------------------------------
.. kernel-doc:: sound/core/sound.c

Memory Management Helpers
-------------------------
.. kernel-doc:: sound/core/memory.c
.. kernel-doc:: sound/core/memalloc.c


PCM API
=======

PCM Core
--------
.. kernel-doc:: sound/core/pcm.c
.. kernel-doc:: sound/core/pcm_lib.c
.. kernel-doc:: sound/core/pcm_native.c
.. kernel-doc:: include/sound/pcm.h

PCM Format Helpers
------------------
.. kernel-doc:: sound/core/pcm_misc.c

PCM Memory Management
---------------------
.. kernel-doc:: sound/core/pcm_memory.c

PCM DMA Engine API
------------------
.. kernel-doc:: sound/core/pcm_dmaengine.c
.. kernel-doc:: include/sound/dmaengine_pcm.h

Control/Mixer API
=================

General Control Interface
-------------------------
.. kernel-doc:: sound/core/control.c

AC97 Codec API
--------------
.. kernel-doc:: sound/pci/ac97/ac97_codec.c
.. kernel-doc:: sound/pci/ac97/ac97_pcm.c

Virtual Master Control API
--------------------------
.. kernel-doc:: sound/core/vmaster.c
.. kernel-doc:: include/sound/control.h

MIDI API
========

Raw MIDI API
------------
.. kernel-doc:: sound/core/rawmidi.c

MPU401-UART API
---------------
.. kernel-doc:: sound/drivers/mpu401/mpu401_uart.c

Proc Info API
=============

Proc Info Interface
-------------------
.. kernel-doc:: sound/core/info.c

Compress Offload
================

Compress Offload API
--------------------
.. kernel-doc:: sound/core/compress_offload.c
.. kernel-doc:: include/uapi/sound/compress_offload.h
.. kernel-doc:: include/uapi/sound/compress_params.h
.. kernel-doc:: include/sound/compress_driver.h

ASoC
====

ASoC Core API
-------------
.. kernel-doc:: include/sound/soc.h
.. kernel-doc:: sound/soc/soc-core.c
.. kernel-doc:: sound/soc/soc-devres.c
.. kernel-doc:: sound/soc/soc-component.c
.. kernel-doc:: sound/soc/soc-pcm.c
.. kernel-doc:: sound/soc/soc-ops.c
.. kernel-doc:: sound/soc/soc-compress.c

ASoC DAPM API
-------------
.. kernel-doc:: sound/soc/soc-dapm.c

ASoC DMA Engine API
-------------------
.. kernel-doc:: sound/soc/soc-generic-dmaengine-pcm.c

Miscellaneous Functions
=======================

Hardware-Dependent Devices API
------------------------------
.. kernel-doc:: sound/core/hwdep.c

Jack Abstraction Layer API
--------------------------
.. kernel-doc:: include/sound/jack.h
.. kernel-doc:: sound/core/jack.c
.. kernel-doc:: sound/soc/soc-jack.c

ISA DMA Helpers
---------------
.. kernel-doc:: sound/core/isadma.c

Other Helper Macros
-------------------
.. kernel-doc:: include/sound/core.h
