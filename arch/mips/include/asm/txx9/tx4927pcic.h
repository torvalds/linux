/*
 * include/asm-mips/txx9/tx4927pcic.h
 * TX4927 PCI controller definitions.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_TXX9_TX4927PCIC_H
#define __ASM_TXX9_TX4927PCIC_H

#include <linux/pci.h>
#include <linux/irqreturn.h>

struct tx4927_pcic_reg {
	u32 pciid;
	u32 pcistatus;
	u32 pciccrev;
	u32 pcicfg1;
	u32 p2gm0plbase;		/* +10 */
	u32 p2gm0pubase;
	u32 p2gm1plbase;
	u32 p2gm1pubase;
	u32 p2gm2pbase;		/* +20 */
	u32 p2giopbase;
	u32 unused0;
	u32 pcisid;
	u32 unused1;		/* +30 */
	u32 pcicapptr;
	u32 unused2;
	u32 pcicfg2;
	u32 g2ptocnt;		/* +40 */
	u32 unused3[15];
	u32 g2pstatus;		/* +80 */
	u32 g2pmask;
	u32 pcisstatus;
	u32 pcimask;
	u32 p2gcfg;		/* +90 */
	u32 p2gstatus;
	u32 p2gmask;
	u32 p2gccmd;
	u32 unused4[24];		/* +a0 */
	u32 pbareqport;		/* +100 */
	u32 pbacfg;
	u32 pbastatus;
	u32 pbamask;
	u32 pbabm;		/* +110 */
	u32 pbacreq;
	u32 pbacgnt;
	u32 pbacstate;
	u64 g2pmgbase[3];		/* +120 */
	u64 g2piogbase;
	u32 g2pmmask[3];		/* +140 */
	u32 g2piomask;
	u64 g2pmpbase[3];		/* +150 */
	u64 g2piopbase;
	u32 pciccfg;		/* +170 */
	u32 pcicstatus;
	u32 pcicmask;
	u32 unused5;
	u64 p2gmgbase[3];		/* +180 */
	u64 p2giogbase;
	u32 g2pcfgadrs;		/* +1a0 */
	u32 g2pcfgdata;
	u32 unused6[8];
	u32 g2pintack;
	u32 g2pspc;
	u32 unused7[12];		/* +1d0 */
	u64 pdmca;		/* +200 */
	u64 pdmga;
	u64 pdmpa;
	u64 pdmctr;
	u64 pdmcfg;		/* +220 */
	u64 pdmsts;
};

/* bits for PCICMD */
/* see PCI_COMMAND_XXX in linux/pci_regs.h */

/* bits for PCISTAT */
/* see PCI_STATUS_XXX in linux/pci_regs.h */

/* bits for IOBA/MBA */
/* see PCI_BASE_ADDRESS_XXX in linux/pci_regs.h */

/* bits for G2PSTATUS/G2PMASK */
#define TX4927_PCIC_G2PSTATUS_ALL	0x00000003
#define TX4927_PCIC_G2PSTATUS_TTOE	0x00000002
#define TX4927_PCIC_G2PSTATUS_RTOE	0x00000001

/* bits for PCIMASK (see also PCI_STATUS_XXX in linux/pci_regs.h */
#define TX4927_PCIC_PCISTATUS_ALL	0x0000f900

/* bits for PBACFG */
#define TX4927_PCIC_PBACFG_FIXPA	0x00000008
#define TX4927_PCIC_PBACFG_RPBA 0x00000004
#define TX4927_PCIC_PBACFG_PBAEN	0x00000002
#define TX4927_PCIC_PBACFG_BMCEN	0x00000001

/* bits for PBASTATUS/PBAMASK */
#define TX4927_PCIC_PBASTATUS_ALL	0x00000001
#define TX4927_PCIC_PBASTATUS_BM	0x00000001

/* bits for G2PMnGBASE */
#define TX4927_PCIC_G2PMnGBASE_BSDIS	0x0000002000000000ULL
#define TX4927_PCIC_G2PMnGBASE_ECHG	0x0000001000000000ULL

/* bits for G2PIOGBASE */
#define TX4927_PCIC_G2PIOGBASE_BSDIS	0x0000002000000000ULL
#define TX4927_PCIC_G2PIOGBASE_ECHG	0x0000001000000000ULL

/* bits for PCICSTATUS/PCICMASK */
#define TX4927_PCIC_PCICSTATUS_ALL	0x000007b8
#define TX4927_PCIC_PCICSTATUS_PME	0x00000400
#define TX4927_PCIC_PCICSTATUS_TLB	0x00000200
#define TX4927_PCIC_PCICSTATUS_NIB	0x00000100
#define TX4927_PCIC_PCICSTATUS_ZIB	0x00000080
#define TX4927_PCIC_PCICSTATUS_PERR	0x00000020
#define TX4927_PCIC_PCICSTATUS_SERR	0x00000010
#define TX4927_PCIC_PCICSTATUS_GBE	0x00000008
#define TX4927_PCIC_PCICSTATUS_IWB	0x00000002
#define TX4927_PCIC_PCICSTATUS_E2PDONE	0x00000001

/* bits for PCICCFG */
#define TX4927_PCIC_PCICCFG_GBWC_MASK	0x0fff0000
#define TX4927_PCIC_PCICCFG_HRST	0x00000800
#define TX4927_PCIC_PCICCFG_SRST	0x00000400
#define TX4927_PCIC_PCICCFG_IRBER	0x00000200
#define TX4927_PCIC_PCICCFG_G2PMEN(ch)	(0x00000100>>(ch))
#define TX4927_PCIC_PCICCFG_G2PM0EN	0x00000100
#define TX4927_PCIC_PCICCFG_G2PM1EN	0x00000080
#define TX4927_PCIC_PCICCFG_G2PM2EN	0x00000040
#define TX4927_PCIC_PCICCFG_G2PIOEN	0x00000020
#define TX4927_PCIC_PCICCFG_TCAR	0x00000010
#define TX4927_PCIC_PCICCFG_ICAEN	0x00000008

/* bits for P2GMnGBASE */
#define TX4927_PCIC_P2GMnGBASE_TMEMEN	0x0000004000000000ULL
#define TX4927_PCIC_P2GMnGBASE_TBSDIS	0x0000002000000000ULL
#define TX4927_PCIC_P2GMnGBASE_TECHG	0x0000001000000000ULL

/* bits for P2GIOGBASE */
#define TX4927_PCIC_P2GIOGBASE_TIOEN	0x0000004000000000ULL
#define TX4927_PCIC_P2GIOGBASE_TBSDIS	0x0000002000000000ULL
#define TX4927_PCIC_P2GIOGBASE_TECHG	0x0000001000000000ULL

#define TX4927_PCIC_IDSEL_AD_TO_SLOT(ad)	((ad) - 11)
#define TX4927_PCIC_MAX_DEVNU	TX4927_PCIC_IDSEL_AD_TO_SLOT(32)

/* bits for PDMCFG */
#define TX4927_PCIC_PDMCFG_RSTFIFO	0x00200000
#define TX4927_PCIC_PDMCFG_EXFER	0x00100000
#define TX4927_PCIC_PDMCFG_REQDLY_MASK	0x00003800
#define TX4927_PCIC_PDMCFG_REQDLY_NONE	(0 << 11)
#define TX4927_PCIC_PDMCFG_REQDLY_16	(1 << 11)
#define TX4927_PCIC_PDMCFG_REQDLY_32	(2 << 11)
#define TX4927_PCIC_PDMCFG_REQDLY_64	(3 << 11)
#define TX4927_PCIC_PDMCFG_REQDLY_128	(4 << 11)
#define TX4927_PCIC_PDMCFG_REQDLY_256	(5 << 11)
#define TX4927_PCIC_PDMCFG_REQDLY_512	(6 << 11)
#define TX4927_PCIC_PDMCFG_REQDLY_1024	(7 << 11)
#define TX4927_PCIC_PDMCFG_ERRIE	0x00000400
#define TX4927_PCIC_PDMCFG_NCCMPIE	0x00000200
#define TX4927_PCIC_PDMCFG_NTCMPIE	0x00000100
#define TX4927_PCIC_PDMCFG_CHNEN	0x00000080
#define TX4927_PCIC_PDMCFG_XFRACT	0x00000040
#define TX4927_PCIC_PDMCFG_BSWAP	0x00000020
#define TX4927_PCIC_PDMCFG_XFRSIZE_MASK 0x0000000c
#define TX4927_PCIC_PDMCFG_XFRSIZE_1DW	0x00000000
#define TX4927_PCIC_PDMCFG_XFRSIZE_1QW	0x00000004
#define TX4927_PCIC_PDMCFG_XFRSIZE_4QW	0x00000008
#define TX4927_PCIC_PDMCFG_XFRDIRC	0x00000002
#define TX4927_PCIC_PDMCFG_CHRST	0x00000001

/* bits for PDMSTS */
#define TX4927_PCIC_PDMSTS_REQCNT_MASK	0x3f000000
#define TX4927_PCIC_PDMSTS_FIFOCNT_MASK 0x00f00000
#define TX4927_PCIC_PDMSTS_FIFOWP_MASK	0x000c0000
#define TX4927_PCIC_PDMSTS_FIFORP_MASK	0x00030000
#define TX4927_PCIC_PDMSTS_ERRINT	0x00000800
#define TX4927_PCIC_PDMSTS_DONEINT	0x00000400
#define TX4927_PCIC_PDMSTS_CHNEN	0x00000200
#define TX4927_PCIC_PDMSTS_XFRACT	0x00000100
#define TX4927_PCIC_PDMSTS_ACCMP	0x00000080
#define TX4927_PCIC_PDMSTS_NCCMP	0x00000040
#define TX4927_PCIC_PDMSTS_NTCMP	0x00000020
#define TX4927_PCIC_PDMSTS_CFGERR	0x00000008
#define TX4927_PCIC_PDMSTS_PCIERR	0x00000004
#define TX4927_PCIC_PDMSTS_CHNERR	0x00000002
#define TX4927_PCIC_PDMSTS_DATAERR	0x00000001
#define TX4927_PCIC_PDMSTS_ALL_CMP	0x000000e0
#define TX4927_PCIC_PDMSTS_ALL_ERR	0x0000000f

struct tx4927_pcic_reg __iomem *get_tx4927_pcicptr(
	struct pci_controller *channel);
void tx4927_pcic_setup(struct tx4927_pcic_reg __iomem *pcicptr,
		       struct pci_controller *channel, int extarb);
void tx4927_report_pcic_status(void);
char *tx4927_pcibios_setup(char *str);
void tx4927_dump_pcic_settings(void);
irqreturn_t tx4927_pcierr_interrupt(int irq, void *dev_id);

#endif /* __ASM_TXX9_TX4927PCIC_H */
