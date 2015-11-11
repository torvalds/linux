/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

/**
 * Interface definitions for the trio driver.
 */

#ifndef _SYS_HV_DRV_TRIO_INTF_H
#define _SYS_HV_DRV_TRIO_INTF_H

#include <arch/trio.h>

/** The vendor ID for all Tilera processors. */
#define TILERA_VENDOR_ID 0x1a41

/** The device ID for the Gx36 processor. */
#define TILERA_GX36_DEV_ID 0x0200

/** Device ID for our internal bridge when running as RC. */
#define TILERA_GX36_RC_DEV_ID 0x2000

/** Maximum number of TRIO interfaces. */
#define TILEGX_NUM_TRIO         2

/** Gx36 has max 3 PCIe MACs per TRIO interface. */
#define TILEGX_TRIO_PCIES       3

/** Specify port properties for a PCIe MAC. */
struct pcie_port_property
{
  /** If true, the link can be configured in PCIe root complex mode. */
  uint8_t allow_rc: 1;

  /** If true, the link can be configured in PCIe endpoint mode. */
  uint8_t allow_ep: 1;

  /** If true, the link can be configured in StreamIO mode. */
  uint8_t allow_sio: 1;

  /** If true, the link is allowed to support 1-lane operation. Software
   *  will not consider it an error if the link comes up as a x1 link. */
  uint8_t allow_x1: 1;

  /** If true, the link is allowed to support 2-lane operation. Software
   *  will not consider it an error if the link comes up as a x2 link. */
  uint8_t allow_x2: 1;

  /** If true, the link is allowed to support 4-lane operation. Software
   *  will not consider it an error if the link comes up as a x4 link. */
  uint8_t allow_x4: 1;

  /** If true, the link is allowed to support 8-lane operation. Software
   *  will not consider it an error if the link comes up as a x8 link. */
  uint8_t allow_x8: 1;

  /** If true, this link is connected to a device which may or may not
   *  be present. */
  uint8_t removable: 1;

};

/** Configurations can be issued to configure a char stream interrupt. */
typedef enum pcie_stream_intr_config_sel_e
{
  /** Interrupt configuration for memory map regions. */
  MEM_MAP_SEL,

  /** Interrupt configuration for push DMAs. */
  PUSH_DMA_SEL,

  /** Interrupt configuration for pull DMAs. */
  PULL_DMA_SEL,
}
pcie_stream_intr_config_sel_t;


/** The mmap file offset (PA) of the TRIO config region. */
#define HV_TRIO_CONFIG_OFFSET                                        \
  ((unsigned long long)TRIO_MMIO_ADDRESS_SPACE__REGION_VAL_CFG <<   \
    TRIO_MMIO_ADDRESS_SPACE__REGION_SHIFT)

/** The maximum size of the TRIO config region. */
#define HV_TRIO_CONFIG_SIZE                                 \
  (1ULL << TRIO_CFG_REGION_ADDR__REGION_SHIFT)

/** Size of the config region mapped into client. We can't use
 *  TRIO_MMIO_ADDRESS_SPACE__OFFSET_WIDTH because it
 *  will require the kernel to allocate 4GB VA space
 *  from the VMALLOC region which has a total range
 *  of 4GB.
 */
#define HV_TRIO_CONFIG_IOREMAP_SIZE                            \
  ((uint64_t) 1 << TRIO_CFG_REGION_ADDR__PROT_SHIFT)

/** The mmap file offset (PA) of a scatter queue region. */
#define HV_TRIO_SQ_OFFSET(queue)                                        \
  (((unsigned long long)TRIO_MMIO_ADDRESS_SPACE__REGION_VAL_MAP_SQ <<   \
    TRIO_MMIO_ADDRESS_SPACE__REGION_SHIFT) |                            \
   ((queue) << TRIO_MAP_SQ_REGION_ADDR__SQ_SEL_SHIFT))

/** The maximum size of a scatter queue region. */
#define HV_TRIO_SQ_SIZE                                 \
  (1ULL << TRIO_MAP_SQ_REGION_ADDR__SQ_SEL_SHIFT)


/** The "hardware MMIO region" of the first PIO region. */
#define HV_TRIO_FIRST_PIO_REGION 8

/** The mmap file offset (PA) of a PIO region. */
#define HV_TRIO_PIO_OFFSET(region)                           \
  (((unsigned long long)(region) + HV_TRIO_FIRST_PIO_REGION) \
   << TRIO_PIO_REGIONS_ADDR__REGION_SHIFT)

/** The maximum size of a PIO region. */
#define HV_TRIO_PIO_SIZE (1ULL << TRIO_PIO_REGIONS_ADDR__ADDR_WIDTH)


/** The mmap file offset (PA) of a push DMA region. */
#define HV_TRIO_PUSH_DMA_OFFSET(ring)                                   \
  (((unsigned long long)TRIO_MMIO_ADDRESS_SPACE__REGION_VAL_PUSH_DMA << \
    TRIO_MMIO_ADDRESS_SPACE__REGION_SHIFT) |                            \
   ((ring) << TRIO_PUSH_DMA_REGION_ADDR__RING_SEL_SHIFT))

/** The mmap file offset (PA) of a pull DMA region. */
#define HV_TRIO_PULL_DMA_OFFSET(ring)                                   \
  (((unsigned long long)TRIO_MMIO_ADDRESS_SPACE__REGION_VAL_PULL_DMA << \
    TRIO_MMIO_ADDRESS_SPACE__REGION_SHIFT) |                            \
   ((ring) << TRIO_PULL_DMA_REGION_ADDR__RING_SEL_SHIFT))

/** The maximum size of a DMA region. */
#define HV_TRIO_DMA_REGION_SIZE                         \
  (1ULL << TRIO_PUSH_DMA_REGION_ADDR__RING_SEL_SHIFT)


/** The mmap file offset (PA) of a Mem-Map interrupt region. */
#define HV_TRIO_MEM_MAP_INTR_OFFSET(map)                                 \
  (((unsigned long long)TRIO_MMIO_ADDRESS_SPACE__REGION_VAL_MAP_MEM <<   \
    TRIO_MMIO_ADDRESS_SPACE__REGION_SHIFT) |                            \
   ((map) << TRIO_MAP_MEM_REGION_ADDR__MAP_SEL_SHIFT))

/** The maximum size of a Mem-Map interrupt region. */
#define HV_TRIO_MEM_MAP_INTR_SIZE                                 \
  (1ULL << TRIO_MAP_MEM_REGION_ADDR__MAP_SEL_SHIFT)


/** A flag bit indicating a fixed resource allocation. */
#define HV_TRIO_ALLOC_FIXED 0x01

/** TRIO requires that all mappings have 4kB aligned start addresses. */
#define HV_TRIO_PAGE_SHIFT 12

/** TRIO requires that all mappings have 4kB aligned start addresses. */
#define HV_TRIO_PAGE_SIZE (1ull << HV_TRIO_PAGE_SHIFT)


/* Specify all PCIe port properties for a TRIO. */
struct pcie_trio_ports_property
{
  struct pcie_port_property ports[TILEGX_TRIO_PCIES];

  /** Set if this TRIO belongs to a Gx72 device. */
  uint8_t is_gx72;
};

/* Flags indicating traffic class. */
#define HV_TRIO_FLAG_TC_SHIFT 4
#define HV_TRIO_FLAG_TC_RMASK 0xf
#define HV_TRIO_FLAG_TC(N) \
  ((((N) & HV_TRIO_FLAG_TC_RMASK) + 1) << HV_TRIO_FLAG_TC_SHIFT)

/* Flags indicating virtual functions. */
#define HV_TRIO_FLAG_VFUNC_SHIFT 8
#define HV_TRIO_FLAG_VFUNC_RMASK 0xff
#define HV_TRIO_FLAG_VFUNC(N) \
  ((((N) & HV_TRIO_FLAG_VFUNC_RMASK) + 1) << HV_TRIO_FLAG_VFUNC_SHIFT)


/* Flag indicating an ordered PIO region. */
#define HV_TRIO_PIO_FLAG_ORDERED (1 << 16)

/* Flags indicating special types of PIO regions. */
#define HV_TRIO_PIO_FLAG_SPACE_SHIFT 17
#define HV_TRIO_PIO_FLAG_SPACE_MASK (0x3 << HV_TRIO_PIO_FLAG_SPACE_SHIFT)
#define HV_TRIO_PIO_FLAG_CONFIG_SPACE (0x1 << HV_TRIO_PIO_FLAG_SPACE_SHIFT)
#define HV_TRIO_PIO_FLAG_IO_SPACE (0x2 << HV_TRIO_PIO_FLAG_SPACE_SHIFT)


#endif /* _SYS_HV_DRV_TRIO_INTF_H */
