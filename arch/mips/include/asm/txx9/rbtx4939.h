/*
 * Definitions for RBTX4939
 *
 * (C) Copyright TOSHIBA CORPORATION 2005-2006
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef __ASM_TXX9_RBTX4939_H
#define __ASM_TXX9_RBTX4939_H

#include <asm/addrspace.h>
#include <asm/txx9irq.h>
#include <asm/txx9/generic.h>
#include <asm/txx9/tx4939.h>

/* Address map */
#define RBTX4939_IOC_REG_ADDR	(IO_BASE + TXX9_CE(1) + 0x00000000)
#define RBTX4939_BOARD_REV_ADDR (IO_BASE + TXX9_CE(1) + 0x00000000)
#define RBTX4939_IOC_REV_ADDR	(IO_BASE + TXX9_CE(1) + 0x00000002)
#define RBTX4939_CONFIG1_ADDR	(IO_BASE + TXX9_CE(1) + 0x00000004)
#define RBTX4939_CONFIG2_ADDR	(IO_BASE + TXX9_CE(1) + 0x00000006)
#define RBTX4939_CONFIG3_ADDR	(IO_BASE + TXX9_CE(1) + 0x00000008)
#define RBTX4939_CONFIG4_ADDR	(IO_BASE + TXX9_CE(1) + 0x0000000a)
#define RBTX4939_USTAT_ADDR	(IO_BASE + TXX9_CE(1) + 0x00001000)
#define RBTX4939_UDIPSW_ADDR	(IO_BASE + TXX9_CE(1) + 0x00001002)
#define RBTX4939_BDIPSW_ADDR	(IO_BASE + TXX9_CE(1) + 0x00001004)
#define RBTX4939_IEN_ADDR	(IO_BASE + TXX9_CE(1) + 0x00002000)
#define RBTX4939_IPOL_ADDR	(IO_BASE + TXX9_CE(1) + 0x00002002)
#define RBTX4939_IFAC1_ADDR	(IO_BASE + TXX9_CE(1) + 0x00002004)
#define RBTX4939_IFAC2_ADDR	(IO_BASE + TXX9_CE(1) + 0x00002006)
#define RBTX4939_SOFTINT_ADDR	(IO_BASE + TXX9_CE(1) + 0x00003000)
#define RBTX4939_ISASTAT_ADDR	(IO_BASE + TXX9_CE(1) + 0x00004000)
#define RBTX4939_PCISTAT_ADDR	(IO_BASE + TXX9_CE(1) + 0x00004002)
#define RBTX4939_ROME_ADDR	(IO_BASE + TXX9_CE(1) + 0x00004004)
#define RBTX4939_SPICS_ADDR	(IO_BASE + TXX9_CE(1) + 0x00004006)
#define RBTX4939_AUDI_ADDR	(IO_BASE + TXX9_CE(1) + 0x00004008)
#define RBTX4939_ISAGPIO_ADDR	(IO_BASE + TXX9_CE(1) + 0x0000400a)
#define RBTX4939_PE1_ADDR	(IO_BASE + TXX9_CE(1) + 0x00005000)
#define RBTX4939_PE2_ADDR	(IO_BASE + TXX9_CE(1) + 0x00005002)
#define RBTX4939_PE3_ADDR	(IO_BASE + TXX9_CE(1) + 0x00005004)
#define RBTX4939_VP_ADDR	(IO_BASE + TXX9_CE(1) + 0x00005006)
#define RBTX4939_VPRESET_ADDR	(IO_BASE + TXX9_CE(1) + 0x00005008)
#define RBTX4939_VPSOUT_ADDR	(IO_BASE + TXX9_CE(1) + 0x0000500a)
#define RBTX4939_VPSIN_ADDR	(IO_BASE + TXX9_CE(1) + 0x0000500c)
#define RBTX4939_7SEG_ADDR(s, ch)	\
	(IO_BASE + TXX9_CE(1) + 0x00006000 + (s) * 16 + ((ch) & 3) * 2)
#define RBTX4939_SOFTRESET_ADDR (IO_BASE + TXX9_CE(1) + 0x00007000)
#define RBTX4939_RESETEN_ADDR	(IO_BASE + TXX9_CE(1) + 0x00007002)
#define RBTX4939_RESETSTAT_ADDR (IO_BASE + TXX9_CE(1) + 0x00007004)
#define RBTX4939_ETHER_BASE	(IO_BASE + TXX9_CE(1) + 0x00020000)

/* Ethernet port address */
#define RBTX4939_ETHER_ADDR	(RBTX4939_ETHER_BASE + 0x300)

/* bits for IEN/IPOL/IFAC */
#define RBTX4938_INTB_ISA0	0
#define RBTX4938_INTB_ISA11	1
#define RBTX4938_INTB_ISA12	2
#define RBTX4938_INTB_ISA15	3
#define RBTX4938_INTB_I2S	4
#define RBTX4938_INTB_SW	5
#define RBTX4938_INTF_ISA0	(1 << RBTX4938_INTB_ISA0)
#define RBTX4938_INTF_ISA11	(1 << RBTX4938_INTB_ISA11)
#define RBTX4938_INTF_ISA12	(1 << RBTX4938_INTB_ISA12)
#define RBTX4938_INTF_ISA15	(1 << RBTX4938_INTB_ISA15)
#define RBTX4938_INTF_I2S	(1 << RBTX4938_INTB_I2S)
#define RBTX4938_INTF_SW	(1 << RBTX4938_INTB_SW)

/* bits for PE1,PE2,PE3 */
#define RBTX4939_PE1_ATA(ch)	(0x01 << (ch))
#define RBTX4939_PE1_RMII(ch)	(0x04 << (ch))
#define RBTX4939_PE2_SIO0	0x01
#define RBTX4939_PE2_SIO2	0x02
#define RBTX4939_PE2_SIO3	0x04
#define RBTX4939_PE2_CIR	0x08
#define RBTX4939_PE2_SPI	0x10
#define RBTX4939_PE2_GPIO	0x20
#define RBTX4939_PE3_VP 0x01
#define RBTX4939_PE3_VP_P	0x02
#define RBTX4939_PE3_VP_S	0x04

#define rbtx4939_board_rev_addr ((u8 __iomem *)RBTX4939_BOARD_REV_ADDR)
#define rbtx4939_ioc_rev_addr	((u8 __iomem *)RBTX4939_IOC_REV_ADDR)
#define rbtx4939_config1_addr	((u8 __iomem *)RBTX4939_CONFIG1_ADDR)
#define rbtx4939_config2_addr	((u8 __iomem *)RBTX4939_CONFIG2_ADDR)
#define rbtx4939_config3_addr	((u8 __iomem *)RBTX4939_CONFIG3_ADDR)
#define rbtx4939_config4_addr	((u8 __iomem *)RBTX4939_CONFIG4_ADDR)
#define rbtx4939_ustat_addr	((u8 __iomem *)RBTX4939_USTAT_ADDR)
#define rbtx4939_udipsw_addr	((u8 __iomem *)RBTX4939_UDIPSW_ADDR)
#define rbtx4939_bdipsw_addr	((u8 __iomem *)RBTX4939_BDIPSW_ADDR)
#define rbtx4939_ien_addr	((u8 __iomem *)RBTX4939_IEN_ADDR)
#define rbtx4939_ipol_addr	((u8 __iomem *)RBTX4939_IPOL_ADDR)
#define rbtx4939_ifac1_addr	((u8 __iomem *)RBTX4939_IFAC1_ADDR)
#define rbtx4939_ifac2_addr	((u8 __iomem *)RBTX4939_IFAC2_ADDR)
#define rbtx4939_softint_addr	((u8 __iomem *)RBTX4939_SOFTINT_ADDR)
#define rbtx4939_isastat_addr	((u8 __iomem *)RBTX4939_ISASTAT_ADDR)
#define rbtx4939_pcistat_addr	((u8 __iomem *)RBTX4939_PCISTAT_ADDR)
#define rbtx4939_rome_addr	((u8 __iomem *)RBTX4939_ROME_ADDR)
#define rbtx4939_spics_addr	((u8 __iomem *)RBTX4939_SPICS_ADDR)
#define rbtx4939_audi_addr	((u8 __iomem *)RBTX4939_AUDI_ADDR)
#define rbtx4939_isagpio_addr	((u8 __iomem *)RBTX4939_ISAGPIO_ADDR)
#define rbtx4939_pe1_addr	((u8 __iomem *)RBTX4939_PE1_ADDR)
#define rbtx4939_pe2_addr	((u8 __iomem *)RBTX4939_PE2_ADDR)
#define rbtx4939_pe3_addr	((u8 __iomem *)RBTX4939_PE3_ADDR)
#define rbtx4939_vp_addr	((u8 __iomem *)RBTX4939_VP_ADDR)
#define rbtx4939_vpreset_addr	((u8 __iomem *)RBTX4939_VPRESET_ADDR)
#define rbtx4939_vpsout_addr	((u8 __iomem *)RBTX4939_VPSOUT_ADDR)
#define rbtx4939_vpsin_addr	((u8 __iomem *)RBTX4939_VPSIN_ADDR)
#define rbtx4939_7seg_addr(s, ch) \
				((u8 __iomem *)RBTX4939_7SEG_ADDR(s, ch))
#define rbtx4939_softreset_addr ((u8 __iomem *)RBTX4939_SOFTRESET_ADDR)
#define rbtx4939_reseten_addr	((u8 __iomem *)RBTX4939_RESETEN_ADDR)
#define rbtx4939_resetstat_addr ((u8 __iomem *)RBTX4939_RESETSTAT_ADDR)

/*
 * IRQ mappings
 */
#define RBTX4939_NR_IRQ_IOC	8

#define RBTX4939_IRQ_IOC	(TXX9_IRQ_BASE + TX4939_NUM_IR)
#define RBTX4939_IRQ_END	(RBTX4939_IRQ_IOC + RBTX4939_NR_IRQ_IOC)

/* IOC (ISA, etc) */
#define RBTX4939_IRQ_IOCINT	(TXX9_IRQ_BASE + TX4939_IR_INT(0))
/* Onboard 10M Ether */
#define RBTX4939_IRQ_ETHER	(TXX9_IRQ_BASE + TX4939_IR_INT(1))

void rbtx4939_prom_init(void);
void rbtx4939_irq_setup(void);

struct mtd_partition;
struct map_info;
struct rbtx4939_flash_data {
	unsigned int width;
	unsigned int nr_parts;
	struct mtd_partition *parts;
	void (*map_init)(struct map_info *map);
};

#endif /* __ASM_TXX9_RBTX4939_H */
