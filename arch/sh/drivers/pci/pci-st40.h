/* 
 * Copyright (C) 2001 David J. Mckay (david.mckay@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.                            
 *
 * Defintions for the ST40 PCI hardware.
 */

#ifndef __PCI_ST40_H__
#define __PCI_ST40_H__

#define ST40PCI_VCR_STATUS    0x00

#define ST40PCI_VCR_VERSION   0x08

#define ST40PCI_CR            0x10

#define CR_SOFT_RESET (1<<12)
#define CR_PFCS       (1<<11)
#define CR_PFE        (1<<9)
#define CR_BMAM       (1<<6)
#define CR_HOST       (1<<5)
#define CR_CLKEN      (1<<4)
#define CR_SOCS       (1<<3)
#define CR_IOCS       (1<<2)
#define CR_RSTCTL     (1<<1)
#define CR_CFINT      (1<<0)
#define CR_LOCK_MASK  0x5a000000


#define ST40PCI_LSR0          0X14
#define ST40PCI_LAR0          0x1c

#define ST40PCI_INT           0x24
#define INT_MNLTDIM           (1<<15)
#define INT_TTADI             (1<<14)
#define INT_TMTO              (1<<9)
#define INT_MDEI              (1<<8)
#define INT_APEDI             (1<<7)
#define INT_SDI               (1<<6)
#define INT_DPEITW            (1<<5)
#define INT_PEDITR            (1<<4)
#define INT_TADIM             (1<<3)
#define INT_MADIM             (1<<2)
#define INT_MWPDI             (1<<1)
#define INT_MRDPEI            (1<<0)


#define ST40PCI_INTM          0x28
#define ST40PCI_AIR           0x2c

#define ST40PCI_CIR           0x30
#define CIR_PIOTEM            (1<<31)
#define CIR_RWTET             (1<<26)

#define ST40PCI_AINT          0x40
#define AINT_MBI              (1<<13)
#define AINT_TBTOI            (1<<12)
#define AINT_MBTOI            (1<<11)
#define AINT_TAI              (1<<3)
#define AINT_MAI              (1<<2)
#define AINT_RDPEI            (1<<1)
#define AINT_WDPE             (1<<0)

#define ST40PCI_AINTM         0x44
#define ST40PCI_BMIR          0x48
#define ST40PCI_PAR           0x4c
#define ST40PCI_MBR           0x50
#define ST40PCI_IOBR          0x54
#define ST40PCI_PINT          0x58
#define ST40PCI_PINTM         0x5c
#define ST40PCI_MBMR          0x70
#define ST40PCI_IOBMR         0x74
#define ST40PCI_PDR           0x78

/* H8 specific registers start here */
#define ST40PCI_WCBAR         0x7c
#define ST40PCI_LOCCFG_UNLOCK 0x34

#define ST40PCI_RBAR0         0x100
#define ST40PCI_RSR0          0x104
#define ST40PCI_RLAR0         0x108

#define ST40PCI_RBAR1         0x110
#define ST40PCI_RSR1          0x114
#define ST40PCI_RLAR1         0x118


#define ST40PCI_RBAR2         0x120
#define ST40PCI_RSR2          0x124
#define ST40PCI_RLAR2         0x128

#define ST40PCI_RBAR3         0x130
#define ST40PCI_RSR3          0x134
#define ST40PCI_RLAR3         0x138

#define ST40PCI_RBAR4         0x140
#define ST40PCI_RSR4          0x144
#define ST40PCI_RLAR4         0x148

#define ST40PCI_RBAR5         0x150
#define ST40PCI_RSR5          0x154
#define ST40PCI_RLAR5         0x158

#define ST40PCI_RBAR6         0x160
#define ST40PCI_RSR6          0x164
#define ST40PCI_RLAR6         0x168

#define ST40PCI_RBAR7         0x170
#define ST40PCI_RSR7          0x174
#define ST40PCI_RLAR7         0x178


#define ST40PCI_RBAR(n)      (0x100+(0x10*(n)))
#define ST40PCI_RSR(n)       (0x104+(0x10*(n)))
#define ST40PCI_RLAR(n)      (0x108+(0x10*(n)))

#define ST40PCI_PERF               0x80
#define PERF_MASTER_WRITE_POSTING  (1<<4)
/* H8 specific registers end here */


/* These are configs space registers */
#define ST40PCI_CSR_VID               0x10000
#define ST40PCI_CSR_DID               0x10002
#define ST40PCI_CSR_CMD               0x10004
#define ST40PCI_CSR_STATUS            0x10006
#define ST40PCI_CSR_MBAR0             0x10010
#define ST40PCI_CSR_TRDY              0x10040
#define ST40PCI_CSR_RETRY             0x10041
#define ST40PCI_CSR_MIT               0x1000d

#define ST40_IO_ADDR 0xb6000000       

#endif /* __PCI_ST40_H__ */
