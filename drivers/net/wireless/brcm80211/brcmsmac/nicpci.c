/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include <defs.h>
#include <soc.h>
#include <chipcommon.h>
#include "aiutils.h"
#include "pub.h"
#include "nicpci.h"

/* SPROM offsets */
#define SRSH_ASPM_OFFSET		4	/* word 4 */
#define SRSH_ASPM_ENB			0x18	/* bit 3, 4 */
#define SRSH_ASPM_L1_ENB		0x10	/* bit 4 */
#define SRSH_ASPM_L0s_ENB		0x8	/* bit 3 */

#define SRSH_PCIE_MISC_CONFIG		5	/* word 5 */
#define SRSH_L23READY_EXIT_NOPERST	0x8000	/* bit 15 */
#define SRSH_CLKREQ_OFFSET_REV5		20	/* word 20 for srom rev <= 5 */
#define SRSH_CLKREQ_ENB			0x0800	/* bit 11 */
#define SRSH_BD_OFFSET                  6	/* word 6 */

/* chipcontrol */
#define CHIPCTRL_4321_PLL_DOWN		0x800000/* serdes PLL down override */

/* MDIO control */
#define MDIOCTL_DIVISOR_MASK		0x7f	/* clock to be used on MDIO */
#define MDIOCTL_DIVISOR_VAL		0x2
#define MDIOCTL_PREAM_EN		0x80	/* Enable preamble sequnce */
#define MDIOCTL_ACCESS_DONE		0x100	/* Transaction complete */

/* MDIO Data */
#define MDIODATA_MASK			0x0000ffff	/* data 2 bytes */
#define MDIODATA_TA			0x00020000	/* Turnaround */

#define MDIODATA_REGADDR_SHF		18		/* Regaddr shift */
#define MDIODATA_REGADDR_MASK		0x007c0000	/* Regaddr Mask */
#define MDIODATA_DEVADDR_SHF		23	/* Physmedia devaddr shift */
#define MDIODATA_DEVADDR_MASK		0x0f800000
						/* Physmedia devaddr Mask */

/* MDIO Data for older revisions < 10 */
#define MDIODATA_REGADDR_SHF_OLD	18	/* Regaddr shift */
#define MDIODATA_REGADDR_MASK_OLD	0x003c0000
						/* Regaddr Mask */
#define MDIODATA_DEVADDR_SHF_OLD	22	/* Physmedia devaddr shift  */
#define MDIODATA_DEVADDR_MASK_OLD	0x0fc00000
						/* Physmedia devaddr Mask */

/* Transactions flags */
#define MDIODATA_WRITE			0x10000000
#define MDIODATA_READ			0x20000000
#define MDIODATA_START			0x40000000

#define MDIODATA_DEV_ADDR		0x0	/* dev address for serdes */
#define	MDIODATA_BLK_ADDR		0x1F	/* blk address for serdes */

/* serdes regs (rev < 10) */
#define MDIODATA_DEV_PLL		0x1d	/* SERDES PLL Dev */
#define MDIODATA_DEV_TX			0x1e	/* SERDES TX Dev */
#define MDIODATA_DEV_RX			0x1f	/* SERDES RX Dev */

/* SERDES RX registers */
#define SERDES_RX_CTRL			1	/* Rx cntrl */
#define SERDES_RX_TIMER1		2	/* Rx Timer1 */
#define SERDES_RX_CDR			6	/* CDR */
#define SERDES_RX_CDRBW			7	/* CDR BW */
/* SERDES RX control register */
#define SERDES_RX_CTRL_FORCE		0x80	/* rxpolarity_force */
#define SERDES_RX_CTRL_POLARITY		0x40	/* rxpolarity_value */

/* SERDES PLL registers */
#define SERDES_PLL_CTRL                 1	/* PLL control reg */
#define PLL_CTRL_FREQDET_EN             0x4000	/* bit 14 is FREQDET on */

/* Linkcontrol reg offset in PCIE Cap */
#define PCIE_CAP_LINKCTRL_OFFSET	16	/* offset in pcie cap */
#define PCIE_CAP_LCREG_ASPML0s		0x01	/* ASPM L0s in linkctrl */
#define PCIE_CAP_LCREG_ASPML1		0x02	/* ASPM L1 in linkctrl */
#define PCIE_CLKREQ_ENAB		0x100	/* CLKREQ Enab in linkctrl */

#define PCIE_ASPM_ENAB			3	/* ASPM L0s & L1 in linkctrl */
#define PCIE_ASPM_L1_ENAB		2	/* ASPM L0s & L1 in linkctrl */
#define PCIE_ASPM_L0s_ENAB		1	/* ASPM L0s & L1 in linkctrl */
#define PCIE_ASPM_DISAB			0	/* ASPM L0s & L1 in linkctrl */

/* Power management threshold */
#define PCIE_L1THRESHOLDTIME_MASK       0xFF00	/* bits 8 - 15 */
#define PCIE_L1THRESHOLDTIME_SHIFT      8	/* PCIE_L1THRESHOLDTIME_SHIFT */
#define PCIE_L1THRESHOLD_WARVAL         0x72	/* WAR value */
#define PCIE_ASPMTIMER_EXTEND		0x01000000
						/* > rev7:
						 * enable extend ASPM timer
						 */

/* different register spaces to access thru pcie indirect access */
#define PCIE_CONFIGREGS		1	/* Access to config space */
#define PCIE_PCIEREGS		2	/* Access to pcie registers */

/* PCIE protocol PHY diagnostic registers */
#define	PCIE_PLP_STATUSREG		0x204	/* Status */

/* Status reg PCIE_PLP_STATUSREG */
#define PCIE_PLP_POLARITYINV_STAT	0x10

/* PCIE protocol DLLP diagnostic registers */
#define PCIE_DLLP_LCREG			0x100	/* Link Control */
#define PCIE_DLLP_PMTHRESHREG		0x128	/* Power Management Threshold */

/* PCIE protocol TLP diagnostic registers */
#define PCIE_TLP_WORKAROUNDSREG		0x004	/* TLP Workarounds */

/* Sonics to PCI translation types */
#define	SBTOPCI_PREF	0x4		/* prefetch enable */
#define	SBTOPCI_BURST	0x8		/* burst enable */
#define	SBTOPCI_RC_READMULTI	0x20	/* memory read multiple */

#define PCI_CLKRUN_DSBL	0x8000	/* Bit 15 forceClkrun */

/* PCI core index in SROM shadow area */
#define SRSH_PI_OFFSET	0	/* first word */
#define SRSH_PI_MASK	0xf000	/* bit 15:12 */
#define SRSH_PI_SHIFT	12	/* bit 15:12 */

#define PCIREGOFFS(field)	offsetof(struct sbpciregs, field)
#define PCIEREGOFFS(field)	offsetof(struct sbpcieregs, field)

/* Sonics side: PCI core and host control registers */
struct sbpciregs {
	u32 control;		/* PCI control */
	u32 PAD[3];
	u32 arbcontrol;		/* PCI arbiter control */
	u32 clkrun;		/* Clkrun Control (>=rev11) */
	u32 PAD[2];
	u32 intstatus;		/* Interrupt status */
	u32 intmask;		/* Interrupt mask */
	u32 sbtopcimailbox;	/* Sonics to PCI mailbox */
	u32 PAD[9];
	u32 bcastaddr;		/* Sonics broadcast address */
	u32 bcastdata;		/* Sonics broadcast data */
	u32 PAD[2];
	u32 gpioin;		/* ro: gpio input (>=rev2) */
	u32 gpioout;		/* rw: gpio output (>=rev2) */
	u32 gpioouten;		/* rw: gpio output enable (>= rev2) */
	u32 gpiocontrol;	/* rw: gpio control (>= rev2) */
	u32 PAD[36];
	u32 sbtopci0;		/* Sonics to PCI translation 0 */
	u32 sbtopci1;		/* Sonics to PCI translation 1 */
	u32 sbtopci2;		/* Sonics to PCI translation 2 */
	u32 PAD[189];
	u32 pcicfg[4][64];	/* 0x400 - 0x7FF, PCI Cfg Space (>=rev8) */
	u16 sprom[36];		/* SPROM shadow Area */
	u32 PAD[46];
};

/* SB side: PCIE core and host control registers */
struct sbpcieregs {
	u32 control;		/* host mode only */
	u32 PAD[2];
	u32 biststatus;		/* bist Status: 0x00C */
	u32 gpiosel;		/* PCIE gpio sel: 0x010 */
	u32 gpioouten;		/* PCIE gpio outen: 0x14 */
	u32 PAD[2];
	u32 intstatus;		/* Interrupt status: 0x20 */
	u32 intmask;		/* Interrupt mask: 0x24 */
	u32 sbtopcimailbox;	/* sb to pcie mailbox: 0x028 */
	u32 PAD[53];
	u32 sbtopcie0;		/* sb to pcie translation 0: 0x100 */
	u32 sbtopcie1;		/* sb to pcie translation 1: 0x104 */
	u32 sbtopcie2;		/* sb to pcie translation 2: 0x108 */
	u32 PAD[5];

	/* pcie core supports in direct access to config space */
	u32 configaddr;	/* pcie config space access: Address field: 0x120 */
	u32 configdata;	/* pcie config space access: Data field: 0x124 */

	/* mdio access to serdes */
	u32 mdiocontrol;	/* controls the mdio access: 0x128 */
	u32 mdiodata;		/* Data to the mdio access: 0x12c */

	/* pcie protocol phy/dllp/tlp register indirect access mechanism */
	u32 pcieindaddr;	/* indirect access to
				 * the internal register: 0x130
				 */
	u32 pcieinddata;	/* Data to/from the internal regsiter: 0x134 */

	u32 clkreqenctrl;	/* >= rev 6, Clkreq rdma control : 0x138 */
	u32 PAD[177];
	u32 pciecfg[4][64];	/* 0x400 - 0x7FF, PCIE Cfg Space */
	u16 sprom[64];		/* SPROM shadow Area */
};

struct pcicore_info {
	struct bcma_device *core;
	struct si_pub *sih;	/* System interconnect handle */
	struct pci_dev *dev;
	u8 pciecap_lcreg_offset;/* PCIE capability LCreg offset
				 * in the config space
				 */
	bool pcie_pr42767;
	u8 pcie_polarity;
	u8 pcie_war_aspm_ovr;	/* Override ASPM/Clkreq settings */

	u8 pmecap_offset;	/* PM Capability offset in the config space */
	bool pmecap;		/* Capable of generating PME */
};

#define PCIE_ASPM(sih)							\
	((ai_get_buscoretype(sih) == PCIE_CORE_ID) &&			\
	 ((ai_get_buscorerev(sih) >= 3) &&				\
	  (ai_get_buscorerev(sih) <= 5)))


/* delay needed between the mdio control/ mdiodata register data access */
static void pr28829_delay(void)
{
	udelay(10);
}

/* Initialize the PCI core.
 * It's caller's responsibility to make sure that this is done only once
 */
struct pcicore_info *pcicore_init(struct si_pub *sih, struct bcma_device *core)
{
	struct pcicore_info *pi;
	u8 cap_ptr;

	/* alloc struct pcicore_info */
	pi = kzalloc(sizeof(struct pcicore_info), GFP_ATOMIC);
	if (pi == NULL)
		return NULL;

	pi->sih = sih;
	pi->dev = core->bus->host_pci;
	pi->core = core;

	cap_ptr = pcicore_find_pci_capability(pi->dev, PCI_CAP_ID_EXP,
						      NULL, NULL);
	pi->pciecap_lcreg_offset = cap_ptr + PCIE_CAP_LINKCTRL_OFFSET;
	return pi;
}

void pcicore_deinit(struct pcicore_info *pch)
{
	kfree(pch);
}

/* return cap_offset if requested capability exists in the PCI config space */
/* Note that it's caller's responsibility to make sure it's a pci bus */
u8
pcicore_find_pci_capability(struct pci_dev *dev, u8 req_cap_id,
			    unsigned char *buf, u32 *buflen)
{
	u8 cap_id;
	u8 cap_ptr = 0;
	u32 bufsize;
	u8 byte_val;

	/* check for Header type 0 */
	pci_read_config_byte(dev, PCI_HEADER_TYPE, &byte_val);
	if ((byte_val & 0x7f) != PCI_HEADER_TYPE_NORMAL)
		goto end;

	/* check if the capability pointer field exists */
	pci_read_config_byte(dev, PCI_STATUS, &byte_val);
	if (!(byte_val & PCI_STATUS_CAP_LIST))
		goto end;

	pci_read_config_byte(dev, PCI_CAPABILITY_LIST, &cap_ptr);
	/* check if the capability pointer is 0x00 */
	if (cap_ptr == 0x00)
		goto end;

	/* loop thru the capability list
	 * and see if the pcie capability exists
	 */

	pci_read_config_byte(dev, cap_ptr, &cap_id);

	while (cap_id != req_cap_id) {
		pci_read_config_byte(dev, cap_ptr + 1, &cap_ptr);
		if (cap_ptr == 0x00)
			break;
		pci_read_config_byte(dev, cap_ptr, &cap_id);
	}
	if (cap_id != req_cap_id)
		goto end;

	/* found the caller requested capability */
	if (buf != NULL && buflen != NULL) {
		u8 cap_data;

		bufsize = *buflen;
		if (!bufsize)
			goto end;
		*buflen = 0;
		/* copy the capability data excluding cap ID and next ptr */
		cap_data = cap_ptr + 2;
		if ((bufsize + cap_data) > PCI_SZPCR)
			bufsize = PCI_SZPCR - cap_data;
		*buflen = bufsize;
		while (bufsize--) {
			pci_read_config_byte(dev, cap_data, buf);
			cap_data++;
			buf++;
		}
	}
end:
	return cap_ptr;
}

/* ***** Register Access API */
static uint
pcie_readreg(struct bcma_device *core, uint addrtype, uint offset)
{
	uint retval = 0xFFFFFFFF;

	switch (addrtype) {
	case PCIE_CONFIGREGS:
		bcma_write32(core, PCIEREGOFFS(configaddr), offset);
		(void)bcma_read32(core, PCIEREGOFFS(configaddr));
		retval = bcma_read32(core, PCIEREGOFFS(configdata));
		break;
	case PCIE_PCIEREGS:
		bcma_write32(core, PCIEREGOFFS(pcieindaddr), offset);
		(void)bcma_read32(core, PCIEREGOFFS(pcieindaddr));
		retval = bcma_read32(core, PCIEREGOFFS(pcieinddata));
		break;
	}

	return retval;
}

static uint pcie_writereg(struct bcma_device *core, uint addrtype,
			  uint offset, uint val)
{
	switch (addrtype) {
	case PCIE_CONFIGREGS:
		bcma_write32(core, PCIEREGOFFS(configaddr), offset);
		bcma_write32(core, PCIEREGOFFS(configdata), val);
		break;
	case PCIE_PCIEREGS:
		bcma_write32(core, PCIEREGOFFS(pcieindaddr), offset);
		bcma_write32(core, PCIEREGOFFS(pcieinddata), val);
		break;
	default:
		break;
	}
	return 0;
}

static bool pcie_mdiosetblock(struct pcicore_info *pi, uint blk)
{
	uint mdiodata, i = 0;
	uint pcie_serdes_spinwait = 200;

	mdiodata = (MDIODATA_START | MDIODATA_WRITE | MDIODATA_TA |
		    (MDIODATA_DEV_ADDR << MDIODATA_DEVADDR_SHF) |
		    (MDIODATA_BLK_ADDR << MDIODATA_REGADDR_SHF) |
		    (blk << 4));
	bcma_write32(pi->core, PCIEREGOFFS(mdiodata), mdiodata);

	pr28829_delay();
	/* retry till the transaction is complete */
	while (i < pcie_serdes_spinwait) {
		if (bcma_read32(pi->core, PCIEREGOFFS(mdiocontrol)) &
		    MDIOCTL_ACCESS_DONE)
			break;

		udelay(1000);
		i++;
	}

	if (i >= pcie_serdes_spinwait)
		return false;

	return true;
}

static int
pcie_mdioop(struct pcicore_info *pi, uint physmedia, uint regaddr, bool write,
	    uint *val)
{
	uint mdiodata;
	uint i = 0;
	uint pcie_serdes_spinwait = 10;

	/* enable mdio access to SERDES */
	bcma_write32(pi->core, PCIEREGOFFS(mdiocontrol),
		     MDIOCTL_PREAM_EN | MDIOCTL_DIVISOR_VAL);

	if (ai_get_buscorerev(pi->sih) >= 10) {
		/* new serdes is slower in rw,
		 * using two layers of reg address mapping
		 */
		if (!pcie_mdiosetblock(pi, physmedia))
			return 1;
		mdiodata = ((MDIODATA_DEV_ADDR << MDIODATA_DEVADDR_SHF) |
			    (regaddr << MDIODATA_REGADDR_SHF));
		pcie_serdes_spinwait *= 20;
	} else {
		mdiodata = ((physmedia << MDIODATA_DEVADDR_SHF_OLD) |
			    (regaddr << MDIODATA_REGADDR_SHF_OLD));
	}

	if (!write)
		mdiodata |= (MDIODATA_START | MDIODATA_READ | MDIODATA_TA);
	else
		mdiodata |= (MDIODATA_START | MDIODATA_WRITE | MDIODATA_TA |
			     *val);

	bcma_write32(pi->core, PCIEREGOFFS(mdiodata), mdiodata);

	pr28829_delay();

	/* retry till the transaction is complete */
	while (i < pcie_serdes_spinwait) {
		if (bcma_read32(pi->core, PCIEREGOFFS(mdiocontrol)) &
		    MDIOCTL_ACCESS_DONE) {
			if (!write) {
				pr28829_delay();
				*val = (bcma_read32(pi->core,
						    PCIEREGOFFS(mdiodata)) &
					MDIODATA_MASK);
			}
			/* Disable mdio access to SERDES */
			bcma_write32(pi->core, PCIEREGOFFS(mdiocontrol), 0);
			return 0;
		}
		udelay(1000);
		i++;
	}

	/* Timed out. Disable mdio access to SERDES. */
	bcma_write32(pi->core, PCIEREGOFFS(mdiocontrol), 0);
	return 1;
}

/* use the mdio interface to read from mdio slaves */
static int
pcie_mdioread(struct pcicore_info *pi, uint physmedia, uint regaddr,
	      uint *regval)
{
	return pcie_mdioop(pi, physmedia, regaddr, false, regval);
}

/* use the mdio interface to write to mdio slaves */
static int
pcie_mdiowrite(struct pcicore_info *pi, uint physmedia, uint regaddr, uint val)
{
	return pcie_mdioop(pi, physmedia, regaddr, true, &val);
}

/* ***** Support functions ***** */
static u8 pcie_clkreq(struct pcicore_info *pi, u32 mask, u32 val)
{
	u32 reg_val;
	u8 offset;

	offset = pi->pciecap_lcreg_offset;
	if (!offset)
		return 0;

	pci_read_config_dword(pi->dev, offset, &reg_val);
	/* set operation */
	if (mask) {
		if (val)
			reg_val |= PCIE_CLKREQ_ENAB;
		else
			reg_val &= ~PCIE_CLKREQ_ENAB;
		pci_write_config_dword(pi->dev, offset, reg_val);
		pci_read_config_dword(pi->dev, offset, &reg_val);
	}
	if (reg_val & PCIE_CLKREQ_ENAB)
		return 1;
	else
		return 0;
}

static void pcie_extendL1timer(struct pcicore_info *pi, bool extend)
{
	u32 w;
	struct si_pub *sih = pi->sih;

	if (ai_get_buscoretype(sih) != PCIE_CORE_ID ||
	    ai_get_buscorerev(sih) < 7)
		return;

	w = pcie_readreg(pi->core, PCIE_PCIEREGS, PCIE_DLLP_PMTHRESHREG);
	if (extend)
		w |= PCIE_ASPMTIMER_EXTEND;
	else
		w &= ~PCIE_ASPMTIMER_EXTEND;
	pcie_writereg(pi->core, PCIE_PCIEREGS, PCIE_DLLP_PMTHRESHREG, w);
	w = pcie_readreg(pi->core, PCIE_PCIEREGS, PCIE_DLLP_PMTHRESHREG);
}

/* centralized clkreq control policy */
static void pcie_clkreq_upd(struct pcicore_info *pi, uint state)
{
	struct si_pub *sih = pi->sih;

	switch (state) {
	case SI_DOATTACH:
		if (PCIE_ASPM(sih))
			pcie_clkreq(pi, 1, 0);
		break;
	case SI_PCIDOWN:
		/* turn on serdes PLL down */
		if (ai_get_buscorerev(sih) == 6) {
			ai_cc_reg(sih,
				  offsetof(struct chipcregs, chipcontrol_addr),
				  ~0, 0);
			ai_cc_reg(sih,
				  offsetof(struct chipcregs, chipcontrol_data),
				  ~0x40, 0);
		} else if (pi->pcie_pr42767) {
			pcie_clkreq(pi, 1, 1);
		}
		break;
	case SI_PCIUP:
		/* turn off serdes PLL down */
		if (ai_get_buscorerev(sih) == 6) {
			ai_cc_reg(sih,
				  offsetof(struct chipcregs, chipcontrol_addr),
				  ~0, 0);
			ai_cc_reg(sih,
				  offsetof(struct chipcregs, chipcontrol_data),
				  ~0x40, 0x40);
		} else if (PCIE_ASPM(sih)) {	/* disable clkreq */
			pcie_clkreq(pi, 1, 0);
		}
		break;
	}
}

/* ***** PCI core WARs ***** */
/* Done only once at attach time */
static void pcie_war_polarity(struct pcicore_info *pi)
{
	u32 w;

	if (pi->pcie_polarity != 0)
		return;

	w = pcie_readreg(pi->core, PCIE_PCIEREGS, PCIE_PLP_STATUSREG);

	/* Detect the current polarity at attach and force that polarity and
	 * disable changing the polarity
	 */
	if ((w & PCIE_PLP_POLARITYINV_STAT) == 0)
		pi->pcie_polarity = SERDES_RX_CTRL_FORCE;
	else
		pi->pcie_polarity = (SERDES_RX_CTRL_FORCE |
				     SERDES_RX_CTRL_POLARITY);
}

/* enable ASPM and CLKREQ if srom doesn't have it */
/* Needs to happen when update to shadow SROM is needed
 *   : Coming out of 'standby'/'hibernate'
 *   : If pcie_war_aspm_ovr state changed
 */
static void pcie_war_aspm_clkreq(struct pcicore_info *pi)
{
	struct si_pub *sih = pi->sih;
	u16 val16;
	u32 w;

	if (!PCIE_ASPM(sih))
		return;

	/* bypass this on QT or VSIM */
	val16 = bcma_read16(pi->core, PCIEREGOFFS(sprom[SRSH_ASPM_OFFSET]));

	val16 &= ~SRSH_ASPM_ENB;
	if (pi->pcie_war_aspm_ovr == PCIE_ASPM_ENAB)
		val16 |= SRSH_ASPM_ENB;
	else if (pi->pcie_war_aspm_ovr == PCIE_ASPM_L1_ENAB)
		val16 |= SRSH_ASPM_L1_ENB;
	else if (pi->pcie_war_aspm_ovr == PCIE_ASPM_L0s_ENAB)
		val16 |= SRSH_ASPM_L0s_ENB;

	bcma_write16(pi->core, PCIEREGOFFS(sprom[SRSH_ASPM_OFFSET]), val16);

	pci_read_config_dword(pi->dev, pi->pciecap_lcreg_offset, &w);
	w &= ~PCIE_ASPM_ENAB;
	w |= pi->pcie_war_aspm_ovr;
	pci_write_config_dword(pi->dev, pi->pciecap_lcreg_offset, w);

	val16 = bcma_read16(pi->core,
			    PCIEREGOFFS(sprom[SRSH_CLKREQ_OFFSET_REV5]));

	if (pi->pcie_war_aspm_ovr != PCIE_ASPM_DISAB) {
		val16 |= SRSH_CLKREQ_ENB;
		pi->pcie_pr42767 = true;
	} else
		val16 &= ~SRSH_CLKREQ_ENB;

	bcma_write16(pi->core, PCIEREGOFFS(sprom[SRSH_CLKREQ_OFFSET_REV5]),
		     val16);
}

/* Apply the polarity determined at the start */
/* Needs to happen when coming out of 'standby'/'hibernate' */
static void pcie_war_serdes(struct pcicore_info *pi)
{
	u32 w = 0;

	if (pi->pcie_polarity != 0)
		pcie_mdiowrite(pi, MDIODATA_DEV_RX, SERDES_RX_CTRL,
			       pi->pcie_polarity);

	pcie_mdioread(pi, MDIODATA_DEV_PLL, SERDES_PLL_CTRL, &w);
	if (w & PLL_CTRL_FREQDET_EN) {
		w &= ~PLL_CTRL_FREQDET_EN;
		pcie_mdiowrite(pi, MDIODATA_DEV_PLL, SERDES_PLL_CTRL, w);
	}
}

/* Fix MISC config to allow coming out of L2/L3-Ready state w/o PRST */
/* Needs to happen when coming out of 'standby'/'hibernate' */
static void pcie_misc_config_fixup(struct pcicore_info *pi)
{
	u16 val16;

	val16 = bcma_read16(pi->core,
			    PCIEREGOFFS(sprom[SRSH_PCIE_MISC_CONFIG]));

	if ((val16 & SRSH_L23READY_EXIT_NOPERST) == 0) {
		val16 |= SRSH_L23READY_EXIT_NOPERST;
		bcma_write16(pi->core,
			     PCIEREGOFFS(sprom[SRSH_PCIE_MISC_CONFIG]), val16);
	}
}

/* quick hack for testing */
/* Needs to happen when coming out of 'standby'/'hibernate' */
static void pcie_war_noplldown(struct pcicore_info *pi)
{
	/* turn off serdes PLL down */
	ai_cc_reg(pi->sih, offsetof(struct chipcregs, chipcontrol),
		  CHIPCTRL_4321_PLL_DOWN, CHIPCTRL_4321_PLL_DOWN);

	/* clear srom shadow backdoor */
	bcma_write16(pi->core, PCIEREGOFFS(sprom[SRSH_BD_OFFSET]), 0);
}

/* Needs to happen when coming out of 'standby'/'hibernate' */
static void pcie_war_pci_setup(struct pcicore_info *pi)
{
	struct si_pub *sih = pi->sih;
	u32 w;

	if (ai_get_buscorerev(sih) == 0 || ai_get_buscorerev(sih) == 1) {
		w = pcie_readreg(pi->core, PCIE_PCIEREGS,
				 PCIE_TLP_WORKAROUNDSREG);
		w |= 0x8;
		pcie_writereg(pi->core, PCIE_PCIEREGS,
			      PCIE_TLP_WORKAROUNDSREG, w);
	}

	if (ai_get_buscorerev(sih) == 1) {
		w = pcie_readreg(pi->core, PCIE_PCIEREGS, PCIE_DLLP_LCREG);
		w |= 0x40;
		pcie_writereg(pi->core, PCIE_PCIEREGS, PCIE_DLLP_LCREG, w);
	}

	if (ai_get_buscorerev(sih) == 0) {
		pcie_mdiowrite(pi, MDIODATA_DEV_RX, SERDES_RX_TIMER1, 0x8128);
		pcie_mdiowrite(pi, MDIODATA_DEV_RX, SERDES_RX_CDR, 0x0100);
		pcie_mdiowrite(pi, MDIODATA_DEV_RX, SERDES_RX_CDRBW, 0x1466);
	} else if (PCIE_ASPM(sih)) {
		/* Change the L1 threshold for better performance */
		w = pcie_readreg(pi->core, PCIE_PCIEREGS,
				 PCIE_DLLP_PMTHRESHREG);
		w &= ~PCIE_L1THRESHOLDTIME_MASK;
		w |= PCIE_L1THRESHOLD_WARVAL << PCIE_L1THRESHOLDTIME_SHIFT;
		pcie_writereg(pi->core, PCIE_PCIEREGS,
			      PCIE_DLLP_PMTHRESHREG, w);

		pcie_war_serdes(pi);

		pcie_war_aspm_clkreq(pi);
	} else if (ai_get_buscorerev(pi->sih) == 7)
		pcie_war_noplldown(pi);

	/* Note that the fix is actually in the SROM,
	 * that's why this is open-ended
	 */
	if (ai_get_buscorerev(pi->sih) >= 6)
		pcie_misc_config_fixup(pi);
}

/* ***** Functions called during driver state changes ***** */
void pcicore_attach(struct pcicore_info *pi, int state)
{
	struct si_pub *sih = pi->sih;
	struct ssb_sprom *sprom = &pi->core->bus->sprom;
	u32 bfl2;

	bfl2 = sprom->boardflags2_lo + (sprom->boardflags2_hi << 16);

	/* Determine if this board needs override */
	if (PCIE_ASPM(sih)) {
		if (bfl2 & BFL2_PCIEWAR_OVR)
			pi->pcie_war_aspm_ovr = PCIE_ASPM_DISAB;
		else
			pi->pcie_war_aspm_ovr = PCIE_ASPM_ENAB;
	}

	/* These need to happen in this order only */
	pcie_war_polarity(pi);

	pcie_war_serdes(pi);

	pcie_war_aspm_clkreq(pi);

	pcie_clkreq_upd(pi, state);

}

void pcicore_hwup(struct pcicore_info *pi)
{
	if (!pi || ai_get_buscoretype(pi->sih) != PCIE_CORE_ID)
		return;

	pcie_war_pci_setup(pi);
}

void pcicore_up(struct pcicore_info *pi, int state)
{
	if (!pi || ai_get_buscoretype(pi->sih) != PCIE_CORE_ID)
		return;

	/* Restore L1 timer for better performance */
	pcie_extendL1timer(pi, true);

	pcie_clkreq_upd(pi, state);
}

/* When the device is going to enter D3 state
 * (or the system is going to enter S3/S4 states)
 */
void pcicore_sleep(struct pcicore_info *pi)
{
	u32 w;

	if (!pi || !PCIE_ASPM(pi->sih))
		return;

	pci_read_config_dword(pi->dev, pi->pciecap_lcreg_offset, &w);
	w &= ~PCIE_CAP_LCREG_ASPML1;
	pci_write_config_dword(pi->dev, pi->pciecap_lcreg_offset, w);

	pi->pcie_pr42767 = false;
}

void pcicore_down(struct pcicore_info *pi, int state)
{
	if (!pi || ai_get_buscoretype(pi->sih) != PCIE_CORE_ID)
		return;

	pcie_clkreq_upd(pi, state);

	/* Reduce L1 timer for better power savings */
	pcie_extendL1timer(pi, false);
}

void pcicore_fixcfg(struct pcicore_info *pi)
{
	struct bcma_device *core = pi->core;
	u16 val16;
	uint regoff;

	regoff = PCIEREGOFFS(sprom[SRSH_PI_OFFSET]);

	val16 = bcma_read16(pi->core, regoff);
	if (((val16 & SRSH_PI_MASK) >> SRSH_PI_SHIFT) !=
	    (u16)core->core_index) {
		val16 = ((u16)core->core_index << SRSH_PI_SHIFT) |
			(val16 & ~SRSH_PI_MASK);
		bcma_write16(pi->core, regoff, val16);
	}
}
