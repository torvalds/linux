===============
AMDGPU Glossary
===============

Here you can find some generic acronyms used in the amdgpu driver. Notice that
we have a dedicated glossary for Display Core at
'Documentation/gpu/amdgpu/display/dc-glossary.rst'.

.. glossary::

    active_cu_number
      The number of CUs that are active on the system.  The number of active
      CUs may be less than SE * SH * CU depending on the board configuration.

    CP
      Command Processor

    CPLIB
      Content Protection Library

    CU
      Compute Unit

    DFS
      Digital Frequency Synthesizer

    ECP
      Enhanced Content Protection

    EOP
      End Of Pipe/Pipeline

    GART
      Graphics Address Remapping Table.  This is the name we use for the GPUVM
      page table used by the GPU kernel driver.  It remaps system resources
      (memory or MMIO space) into the GPU's address space so the GPU can access
      them.  The name GART harkens back to the days of AGP when the platform
      provided an MMU that the GPU could use to get a contiguous view of
      scattered pages for DMA.  The MMU has since moved on to the GPU, but the
      name stuck.

    GC
      Graphics and Compute

    GMC
      Graphic Memory Controller

    GPUVM
      GPU Virtual Memory.  This is the GPU's MMU.  The GPU supports multiple
      virtual address spaces that can be in flight at any given time.  These
      allow the GPU to remap VRAM and system resources into GPU virtual address
      spaces for use by the GPU kernel driver and applications using the GPU.
      These provide memory protection for different applications using the GPU.

    GTT
      Graphics Translation Tables.  This is a memory pool managed through TTM
      which provides access to system resources (memory or MMIO space) for
      use by the GPU. These addresses can be mapped into the "GART" GPUVM page
      table for use by the kernel driver or into per process GPUVM page tables
      for application usage.

    IH
      Interrupt Handler

    HQD
      Hardware Queue Descriptor

    IB
      Indirect Buffer

    IP
        Intellectual Property blocks

    KCQ
      Kernel Compute Queue

    KGQ
      Kernel Graphics Queue

    KIQ
      Kernel Interface Queue

    MEC
      MicroEngine Compute

    MES
      MicroEngine Scheduler

    MMHUB
      Multi-Media HUB

    MQD
      Memory Queue Descriptor

    PPLib
      PowerPlay Library - PowerPlay is the power management component.

    PSP
        Platform Security Processor

    RLC
      RunList Controller

    SDMA
      System DMA

    SE
      Shader Engine

    SH
      SHader array

    SMU
      System Management Unit

    SS
      Spread Spectrum

    VCE
      Video Compression Engine

    VCN
      Video Codec Next
