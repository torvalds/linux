======================
STi ARM Linux Overview
======================

Introduction
------------

  The ST Microelectronics Multimedia and Application Processors range of
  CortexA9 System-on-Chip are supported by the 'STi' platform of
  ARM Linux. Currently STiH407, STiH410 and STiH418 are supported.


configuration
-------------

  The configuration for the STi platform is supported via the multi_v7_defconfig.

Layout
------

  All the files for multiple machine families (STiH407, STiH410, and STiH418)
  are located in the platform code contained in arch/arm/mach-sti

  There is a generic board board-dt.c in the mach folder which support
  Flattened Device Tree, which means, It works with any compatible board with
  Device Trees.


Document Author
---------------

  Srinivas Kandagatla <srinivas.kandagatla@st.com>, (c) 2013 ST Microelectronics
