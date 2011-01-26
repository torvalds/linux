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

#include <linux/delay.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <bcmdefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <siutils.h>
#include <hndsoc.h>
#include <bcmdevs.h>
#include <sbchipc.h>
#include <pci_core.h>
#include <pcie_core.h>
#include <nicpci.h>
#include <pcicfg.h>

typedef struct {
	union {
		sbpcieregs_t *pcieregs;
		struct sbpciregs *pciregs;
	} regs;			/* Memory mapped register to the core */

	si_t *sih;		/* System interconnect handle */
	struct osl_info *osh;		/* OSL handle */
	u8 pciecap_lcreg_offset;	/* PCIE capability LCreg offset in the config space */
	bool pcie_pr42767;
	u8 pcie_polarity;
	u8 pcie_war_aspm_ovr;	/* Override ASPM/Clkreq settings */

	u8 pmecap_offset;	/* PM Capability offset in the config space */
	bool pmecap;		/* Capable of generating PME */
} pcicore_info_t;

/* debug/trace */
#define	PCI_ERROR(args)
#define PCIE_PUB(sih) \
	(((sih)->bustype == PCI_BUS) && ((sih)->buscoretype == PCIE_CORE_ID))

/* routines to access mdio slave device registers */
static bool pcie_mdiosetblock(pcicore_info_t *pi, uint blk);
static int pcie_mdioop(pcicore_info_t *pi, uint physmedia, uint regaddr,
		       bool write, uint *val);
static int pcie_mdiowrite(pcicore_info_t *pi, uint physmedia, uint readdr,
			  uint val);
static int pcie_mdioread(pcicore_info_t *pi, uint physmedia, uint readdr,
			 uint *ret_val);

static void pcie_extendL1timer(pcicore_info_t *pi, bool extend);
static void pcie_clkreq_upd(pcicore_info_t *pi, uint state);

static void pcie_war_aspm_clkreq(pcicore_info_t *pi);
static void pcie_war_serdes(pcicore_info_t *pi);
static void pcie_war_noplldown(pcicore_info_t *pi);
static void pcie_war_polarity(pcicore_info_t *pi);
static void pcie_war_pci_setup(pcicore_info_t *pi);

static bool pcicore_pmecap(pcicore_info_t *pi);

#define PCIE_ASPM(sih)	((PCIE_PUB(sih)) && (((sih)->buscorerev >= 3) && ((sih)->buscorerev <= 5)))


/* delay needed between the mdio control/ mdiodata register data access */
#define PR28829_DELAY() udelay(10)

/* Initialize the PCI core. It's caller's responsibility to make sure that this is done
 * only once
 */
void *pcicore_init(si_t *sih, struct osl_info *osh, void *regs)
{
	pcicore_info_t *pi;

	ASSERT(sih->bustype == PCI_BUS);

	/* alloc pcicore_info_t */
	pi = kzalloc(sizeof(pcicore_info_t), GFP_ATOMIC);
	if (pi == NULL) {
		PCI_ERROR(("pci_attach: malloc failed!\n"));
		return NULL;
	}

	pi->sih = sih;
	pi->osh = osh;

	if (sih->buscoretype == PCIE_CORE_ID) {
		u8 cap_ptr;
		pi->regs.pcieregs = (sbpcieregs_t *) regs;
		cap_ptr =
		    pcicore_find_pci_capability(pi->osh, PCI_CAP_PCIECAP_ID,
						NULL, NULL);
		ASSERT(cap_ptr);
		pi->pciecap_lcreg_offset = cap_ptr + PCIE_CAP_LINKCTRL_OFFSET;
	} else
		pi->regs.pciregs = (struct sbpciregs *) regs;

	return pi;
}

void pcicore_deinit(void *pch)
{
	pcicore_info_t *pi = (pcicore_info_t *) pch;

	if (pi == NULL)
		return;
	kfree(pi);
}

/* return cap_offset if requested capability exists in the PCI config space */
/* Note that it's caller's responsibility to make sure it's a pci bus */
u8
pcicore_find_pci_capability(struct osl_info *osh, u8 req_cap_id,
			    unsigned char *buf, u32 *buflen)
{
	u8 cap_id;
	u8 cap_ptr = 0;
	u32 bufsize;
	u8 byte_val;

	/* check for Header type 0 */
	pci_read_config_byte(osh->pdev, PCI_CFG_HDR, &byte_val);
	if ((byte_val & 0x7f) != PCI_HEADER_NORMAL)
		goto end;

	/* check if the capability pointer field exists */
	pci_read_config_byte(osh->pdev, PCI_CFG_STAT, &byte_val);
	if (!(byte_val & PCI_CAPPTR_PRESENT))
		goto end;

	pci_read_config_byte(osh->pdev, PCI_CFG_CAPPTR, &cap_ptr);
	/* check if the capability pointer is 0x00 */
	if (cap_ptr == 0x00)
		goto end;

	/* loop thr'u the capability list and see if the pcie capabilty exists */

	pci_read_config_byte(osh->pdev, cap_ptr, &cap_id);

	while (cap_id != req_cap_id) {
		pci_read_config_byte(osh->pdev, cap_ptr + 1, &cap_ptr);
		if (cap_ptr == 0x00)
			break;
		pci_read_config_byte(osh->pdev, cap_ptr, &cap_id);
	}
	if (cap_id != req_cap_id) {
		goto end;
	}
	/* found the caller requested capability */
	if ((buf != NULL) && (buflen != NULL)) {
		u8 cap_data;

		bufsize = *buflen;
		if (!bufsize)
			goto end;
		*buflen = 0;
		/* copy the cpability data excluding cap ID and next ptr */
		cap_data = cap_ptr + 2;
		if ((bufsize + cap_data) > SZPCR)
			bufsize = SZPCR - cap_data;
		*buflen = bufsize;
		while (bufsize--) {
			pci_read_config_byte(osh->pdev, cap_data, buf);
			cap_data++;
			buf++;
		}
	}
 end:
	return cap_ptr;
}

/* ***** Register Access API */
uint
pcie_readreg(struct osl_info *osh, sbpcieregs_t *pcieregs, uint addrtype,
	     uint offset)
{
	uint retval = 0xFFFFFFFF;

	ASSERT(pcieregs != NULL);

	switch (addrtype) {
	case PCIE_CONFIGREGS:
		W_REG(osh, (&pcieregs->configaddr), offset);
		(void)R_REG(osh, (&pcieregs->configaddr));
		retval = R_REG(osh, &(pcieregs->configdata));
		break;
	case PCIE_PCIEREGS:
		W_REG(osh, &(pcieregs->pcieindaddr), offset);
		(void)R_REG(osh, (&pcieregs->pcieindaddr));
		retval = R_REG(osh, &(pcieregs->pcieinddata));
		break;
	default:
		ASSERT(0);
		break;
	}

	return retval;
}

uint
pcie_writereg(struct osl_info *osh, sbpcieregs_t *pcieregs, uint addrtype,
	      uint offset, uint val)
{
	ASSERT(pcieregs != NULL);

	switch (addrtype) {
	case PCIE_CONFIGREGS:
		W_REG(osh, (&pcieregs->configaddr), offset);
		W_REG(osh, (&pcieregs->configdata), val);
		break;
	case PCIE_PCIEREGS:
		W_REG(osh, (&pcieregs->pcieindaddr), offset);
		W_REG(osh, (&pcieregs->pcieinddata), val);
		break;
	default:
		ASSERT(0);
		break;
	}
	return 0;
}

static bool pcie_mdiosetblock(pcicore_info_t *pi, uint blk)
{
	sbpcieregs_t *pcieregs = pi->regs.pcieregs;
	uint mdiodata, i = 0;
	uint pcie_serdes_spinwait = 200;

	mdiodata =
	    MDIODATA_START | MDIODATA_WRITE | (MDIODATA_DEV_ADDR <<
					       MDIODATA_DEVADDR_SHF) |
	    (MDIODATA_BLK_ADDR << MDIODATA_REGADDR_SHF) | MDIODATA_TA | (blk <<
									 4);
	W_REG(pi->osh, &pcieregs->mdiodata, mdiodata);

	PR28829_DELAY();
	/* retry till the transaction is complete */
	while (i < pcie_serdes_spinwait) {
		if (R_REG(pi->osh, &(pcieregs->mdiocontrol)) &
		    MDIOCTL_ACCESS_DONE) {
			break;
		}
		udelay(1000);
		i++;
	}

	if (i >= pcie_serdes_spinwait) {
		PCI_ERROR(("pcie_mdiosetblock: timed out\n"));
		return false;
	}

	return true;
}

static int
pcie_mdioop(pcicore_info_t *pi, uint physmedia, uint regaddr, bool write,
	    uint *val)
{
	sbpcieregs_t *pcieregs = pi->regs.pcieregs;
	uint mdiodata;
	uint i = 0;
	uint pcie_serdes_spinwait = 10;

	/* enable mdio access to SERDES */
	W_REG(pi->osh, (&pcieregs->mdiocontrol),
	      MDIOCTL_PREAM_EN | MDIOCTL_DIVISOR_VAL);

	if (pi->sih->buscorerev >= 10) {
		/* new serdes is slower in rw, using two layers of reg address mapping */
		if (!pcie_mdiosetblock(pi, physmedia))
			return 1;
		mdiodata = (MDIODATA_DEV_ADDR << MDIODATA_DEVADDR_SHF) |
		    (regaddr << MDIODATA_REGADDR_SHF);
		pcie_serdes_spinwait *= 20;
	} else {
		mdiodata = (physmedia << MDIODATA_DEVADDR_SHF_OLD) |
		    (regaddr << MDIODATA_REGADDR_SHF_OLD);
	}

	if (!write)
		mdiodata |= (MDIODATA_START | MDIODATA_READ | MDIODATA_TA);
	else
		mdiodata |=
		    (MDIODATA_START | MDIODATA_WRITE | MDIODATA_TA | *val);

	W_REG(pi->osh, &pcieregs->mdiodata, mdiodata);

	PR28829_DELAY();

	/* retry till the transaction is complete */
	while (i < pcie_serdes_spinwait) {
		if (R_REG(pi->osh, &(pcieregs->mdiocontrol)) &
		    MDIOCTL_ACCESS_DONE) {
			if (!write) {
				PR28829_DELAY();
				*val =
				    (R_REG(pi->osh, &(pcieregs->mdiodata)) &
				     MDIODATA_MASK);
			}
			/* Disable mdio access to SERDES */
			W_REG(pi->osh, (&pcieregs->mdiocontrol), 0);
			return 0;
		}
		udelay(1000);
		i++;
	}

	PCI_ERROR(("pcie_mdioop: timed out op: %d\n", write));
	/* Disable mdio access to SERDES */
	W_REG(pi->osh, (&pcieregs->mdiocontrol), 0);
	return 1;
}

/* use the mdio interface to read from mdio slaves */
static int
pcie_mdioread(pcicore_info_t *pi, uint physmedia, uint regaddr, uint *regval)
{
	return pcie_mdioop(pi, physmedia, regaddr, false, regval);
}

/* use the mdio interface to write to mdio slaves */
static int
pcie_mdiowrite(pcicore_info_t *pi, uint physmedia, uint regaddr, uint val)
{
	return pcie_mdioop(pi, physmedia, regaddr, true, &val);
}

/* ***** Support functions ***** */
u8 pcie_clkreq(void *pch, u32 mask, u32 val)
{
	pcicore_info_t *pi = (pcicore_info_t *) pch;
	u32 reg_val;
	u8 offset;

	offset = pi->pciecap_lcreg_offset;
	if (!offset)
		return 0;

	pci_read_config_dword(pi->osh->pdev, offset, &reg_val);
	/* set operation */
	if (mask) {
		if (val)
			reg_val |= PCIE_CLKREQ_ENAB;
		else
			reg_val &= ~PCIE_CLKREQ_ENAB;
		pci_write_config_dword(pi->osh->pdev, offset, reg_val);
		pci_read_config_dword(pi->osh->pdev, offset, &reg_val);
	}
	if (reg_val & PCIE_CLKREQ_ENAB)
		return 1;
	else
		return 0;
}

static void pcie_extendL1timer(pcicore_info_t *pi, bool extend)
{
	u32 w;
	si_t *sih = pi->sih;
	struct osl_info *osh = pi->osh;
	sbpcieregs_t *pcieregs = pi->regs.pcieregs;

	if (!PCIE_PUB(sih) || sih->buscorerev < 7)
		return;

	w = pcie_readreg(osh, pcieregs, PCIE_PCIEREGS, PCIE_DLLP_PMTHRESHREG);
	if (extend)
		w |= PCIE_ASPMTIMER_EXTEND;
	else
		w &= ~PCIE_ASPMTIMER_EXTEND;
	pcie_writereg(osh, pcieregs, PCIE_PCIEREGS, PCIE_DLLP_PMTHRESHREG, w);
	w = pcie_readreg(osh, pcieregs, PCIE_PCIEREGS, PCIE_DLLP_PMTHRESHREG);
}

/* centralized clkreq control policy */
static void pcie_clkreq_upd(pcicore_info_t *pi, uint state)
{
	si_t *sih = pi->sih;
	ASSERT(PCIE_PUB(sih));

	switch (state) {
	case SI_DOATTACH:
		if (PCIE_ASPM(sih))
			pcie_clkreq((void *)pi, 1, 0);
		break;
	case SI_PCIDOWN:
		if (sih->buscorerev == 6) {	/* turn on serdes PLL down */
			si_corereg(sih, SI_CC_IDX,
				   offsetof(chipcregs_t, chipcontrol_addr), ~0,
				   0);
			si_corereg(sih, SI_CC_IDX,
				   offsetof(chipcregs_t, chipcontrol_data),
				   ~0x40, 0);
		} else if (pi->pcie_pr42767) {
			pcie_clkreq((void *)pi, 1, 1);
		}
		break;
	case SI_PCIUP:
		if (sih->buscorerev == 6) {	/* turn off serdes PLL down */
			si_corereg(sih, SI_CC_IDX,
				   offsetof(chipcregs_t, chipcontrol_addr), ~0,
				   0);
			si_corereg(sih, SI_CC_IDX,
				   offsetof(chipcregs_t, chipcontrol_data),
				   ~0x40, 0x40);
		} else if (PCIE_ASPM(sih)) {	/* disable clkreq */
			pcie_clkreq((void *)pi, 1, 0);
		}
		break;
	default:
		ASSERT(0);
		break;
	}
}

/* ***** PCI core WARs ***** */
/* Done only once at attach time */
static void pcie_war_polarity(pcicore_info_t *pi)
{
	u32 w;

	if (pi->pcie_polarity != 0)
		return;

	w = pcie_readreg(pi->osh, pi->regs.pcieregs, PCIE_PCIEREGS,
			 PCIE_PLP_STATUSREG);

	/* Detect the current polarity at attach and force that polarity and
	 * disable changing the polarity
	 */
	if ((w & PCIE_PLP_POLARITYINV_STAT) == 0)
		pi->pcie_polarity = (SERDES_RX_CTRL_FORCE);
	else
		pi->pcie_polarity =
		    (SERDES_RX_CTRL_FORCE | SERDES_RX_CTRL_POLARITY);
}

/* enable ASPM and CLKREQ if srom doesn't have it */
/* Needs to happen when update to shadow SROM is needed
 *   : Coming out of 'standby'/'hibernate'
 *   : If pcie_war_aspm_ovr state changed
 */
static void pcie_war_aspm_clkreq(pcicore_info_t *pi)
{
	sbpcieregs_t *pcieregs = pi->regs.pcieregs;
	si_t *sih = pi->sih;
	u16 val16, *reg16;
	u32 w;

	if (!PCIE_ASPM(sih))
		return;

	/* bypass this on QT or VSIM */
	if (!ISSIM_ENAB(sih)) {

		reg16 = &pcieregs->sprom[SRSH_ASPM_OFFSET];
		val16 = R_REG(pi->osh, reg16);

		val16 &= ~SRSH_ASPM_ENB;
		if (pi->pcie_war_aspm_ovr == PCIE_ASPM_ENAB)
			val16 |= SRSH_ASPM_ENB;
		else if (pi->pcie_war_aspm_ovr == PCIE_ASPM_L1_ENAB)
			val16 |= SRSH_ASPM_L1_ENB;
		else if (pi->pcie_war_aspm_ovr == PCIE_ASPM_L0s_ENAB)
			val16 |= SRSH_ASPM_L0s_ENB;

		W_REG(pi->osh, reg16, val16);

		pci_read_config_dword(pi->osh->pdev, pi->pciecap_lcreg_offset,
					&w);
		w &= ~PCIE_ASPM_ENAB;
		w |= pi->pcie_war_aspm_ovr;
		pci_write_config_dword(pi->osh->pdev,
					pi->pciecap_lcreg_offset, w);
	}

	reg16 = &pcieregs->sprom[SRSH_CLKREQ_OFFSET_REV5];
	val16 = R_REG(pi->osh, reg16);

	if (pi->pcie_war_aspm_ovr != PCIE_ASPM_DISAB) {
		val16 |= SRSH_CLKREQ_ENB;
		pi->pcie_pr42767 = true;
	} else
		val16 &= ~SRSH_CLKREQ_ENB;

	W_REG(pi->osh, reg16, val16);
}

/* Apply the polarity determined at the start */
/* Needs to happen when coming out of 'standby'/'hibernate' */
static void pcie_war_serdes(pcicore_info_t *pi)
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
static void pcie_misc_config_fixup(pcicore_info_t *pi)
{
	sbpcieregs_t *pcieregs = pi->regs.pcieregs;
	u16 val16, *reg16;

	reg16 = &pcieregs->sprom[SRSH_PCIE_MISC_CONFIG];
	val16 = R_REG(pi->osh, reg16);

	if ((val16 & SRSH_L23READY_EXIT_NOPERST) == 0) {
		val16 |= SRSH_L23READY_EXIT_NOPERST;
		W_REG(pi->osh, reg16, val16);
	}
}

/* quick hack for testing */
/* Needs to happen when coming out of 'standby'/'hibernate' */
static void pcie_war_noplldown(pcicore_info_t *pi)
{
	sbpcieregs_t *pcieregs = pi->regs.pcieregs;
	u16 *reg16;

	ASSERT(pi->sih->buscorerev == 7);

	/* turn off serdes PLL down */
	si_corereg(pi->sih, SI_CC_IDX, offsetof(chipcregs_t, chipcontrol),
		   CHIPCTRL_4321_PLL_DOWN, CHIPCTRL_4321_PLL_DOWN);

	/*  clear srom shadow backdoor */
	reg16 = &pcieregs->sprom[SRSH_BD_OFFSET];
	W_REG(pi->osh, reg16, 0);
}

/* Needs to happen when coming out of 'standby'/'hibernate' */
static void pcie_war_pci_setup(pcicore_info_t *pi)
{
	si_t *sih = pi->sih;
	struct osl_info *osh = pi->osh;
	sbpcieregs_t *pcieregs = pi->regs.pcieregs;
	u32 w;

	if ((sih->buscorerev == 0) || (sih->buscorerev == 1)) {
		w = pcie_readreg(osh, pcieregs, PCIE_PCIEREGS,
				 PCIE_TLP_WORKAROUNDSREG);
		w |= 0x8;
		pcie_writereg(osh, pcieregs, PCIE_PCIEREGS,
			      PCIE_TLP_WORKAROUNDSREG, w);
	}

	if (sih->buscorerev == 1) {
		w = pcie_readreg(osh, pcieregs, PCIE_PCIEREGS, PCIE_DLLP_LCREG);
		w |= (0x40);
		pcie_writereg(osh, pcieregs, PCIE_PCIEREGS, PCIE_DLLP_LCREG, w);
	}

	if (sih->buscorerev == 0) {
		pcie_mdiowrite(pi, MDIODATA_DEV_RX, SERDES_RX_TIMER1, 0x8128);
		pcie_mdiowrite(pi, MDIODATA_DEV_RX, SERDES_RX_CDR, 0x0100);
		pcie_mdiowrite(pi, MDIODATA_DEV_RX, SERDES_RX_CDRBW, 0x1466);
	} else if (PCIE_ASPM(sih)) {
		/* Change the L1 threshold for better performance */
		w = pcie_readreg(osh, pcieregs, PCIE_PCIEREGS,
				 PCIE_DLLP_PMTHRESHREG);
		w &= ~(PCIE_L1THRESHOLDTIME_MASK);
		w |= (PCIE_L1THRESHOLD_WARVAL << PCIE_L1THRESHOLDTIME_SHIFT);
		pcie_writereg(osh, pcieregs, PCIE_PCIEREGS,
			      PCIE_DLLP_PMTHRESHREG, w);

		pcie_war_serdes(pi);

		pcie_war_aspm_clkreq(pi);
	} else if (pi->sih->buscorerev == 7)
		pcie_war_noplldown(pi);

	/* Note that the fix is actually in the SROM, that's why this is open-ended */
	if (pi->sih->buscorerev >= 6)
		pcie_misc_config_fixup(pi);
}

void pcie_war_ovr_aspm_update(void *pch, u8 aspm)
{
	pcicore_info_t *pi = (pcicore_info_t *) pch;

	if (!PCIE_ASPM(pi->sih))
		return;

	/* Validate */
	if (aspm > PCIE_ASPM_ENAB)
		return;

	pi->pcie_war_aspm_ovr = aspm;

	/* Update the current state */
	pcie_war_aspm_clkreq(pi);
}

/* ***** Functions called during driver state changes ***** */
void pcicore_attach(void *pch, char *pvars, int state)
{
	pcicore_info_t *pi = (pcicore_info_t *) pch;
	si_t *sih = pi->sih;

	/* Determine if this board needs override */
	if (PCIE_ASPM(sih)) {
		if ((u32) getintvar(pvars, "boardflags2") & BFL2_PCIEWAR_OVR) {
			pi->pcie_war_aspm_ovr = PCIE_ASPM_DISAB;
		} else {
			pi->pcie_war_aspm_ovr = PCIE_ASPM_ENAB;
		}
	}

	/* These need to happen in this order only */
	pcie_war_polarity(pi);

	pcie_war_serdes(pi);

	pcie_war_aspm_clkreq(pi);

	pcie_clkreq_upd(pi, state);

}

void pcicore_hwup(void *pch)
{
	pcicore_info_t *pi = (pcicore_info_t *) pch;

	if (!pi || !PCIE_PUB(pi->sih))
		return;

	pcie_war_pci_setup(pi);
}

void pcicore_up(void *pch, int state)
{
	pcicore_info_t *pi = (pcicore_info_t *) pch;

	if (!pi || !PCIE_PUB(pi->sih))
		return;

	/* Restore L1 timer for better performance */
	pcie_extendL1timer(pi, true);

	pcie_clkreq_upd(pi, state);
}

/* When the device is going to enter D3 state (or the system is going to enter S3/S4 states */
void pcicore_sleep(void *pch)
{
	pcicore_info_t *pi = (pcicore_info_t *) pch;
	u32 w;

	if (!pi || !PCIE_ASPM(pi->sih))
		return;

	pci_read_config_dword(pi->osh->pdev, pi->pciecap_lcreg_offset, &w);
	w &= ~PCIE_CAP_LCREG_ASPML1;
	pci_write_config_dword(pi->osh->pdev, pi->pciecap_lcreg_offset, w);

	pi->pcie_pr42767 = false;
}

void pcicore_down(void *pch, int state)
{
	pcicore_info_t *pi = (pcicore_info_t *) pch;

	if (!pi || !PCIE_PUB(pi->sih))
		return;

	pcie_clkreq_upd(pi, state);

	/* Reduce L1 timer for better power savings */
	pcie_extendL1timer(pi, false);
}

/* ***** Wake-on-wireless-LAN (WOWL) support functions ***** */
/* Just uses PCI config accesses to find out, when needed before sb_attach is done */
bool pcicore_pmecap_fast(struct osl_info *osh)
{
	u8 cap_ptr;
	u32 pmecap;

	cap_ptr =
	    pcicore_find_pci_capability(osh, PCI_CAP_POWERMGMTCAP_ID, NULL,
					NULL);

	if (!cap_ptr)
		return false;

	pci_read_config_dword(osh->pdev, cap_ptr, &pmecap);

	return (pmecap & PME_CAP_PM_STATES) != 0;
}

/* return true if PM capability exists in the pci config space
 * Uses and caches the information using core handle
 */
static bool pcicore_pmecap(pcicore_info_t *pi)
{
	u8 cap_ptr;
	u32 pmecap;

	if (!pi->pmecap_offset) {
		cap_ptr =
		    pcicore_find_pci_capability(pi->osh,
						PCI_CAP_POWERMGMTCAP_ID, NULL,
						NULL);
		if (!cap_ptr)
			return false;

		pi->pmecap_offset = cap_ptr;

		pci_read_config_dword(pi->osh->pdev, pi->pmecap_offset,
					&pmecap);

		/* At least one state can generate PME */
		pi->pmecap = (pmecap & PME_CAP_PM_STATES) != 0;
	}

	return pi->pmecap;
}

/* Enable PME generation */
void pcicore_pmeen(void *pch)
{
	pcicore_info_t *pi = (pcicore_info_t *) pch;
	u32 w;

	/* if not pmecapable return */
	if (!pcicore_pmecap(pi))
		return;

	pci_read_config_dword(pi->osh->pdev, pi->pmecap_offset + PME_CSR_OFFSET,
				&w);
	w |= (PME_CSR_PME_EN);
	pci_write_config_dword(pi->osh->pdev,
				pi->pmecap_offset + PME_CSR_OFFSET, w);
}

/*
 * Return true if PME status set
 */
bool pcicore_pmestat(void *pch)
{
	pcicore_info_t *pi = (pcicore_info_t *) pch;
	u32 w;

	if (!pcicore_pmecap(pi))
		return false;

	pci_read_config_dword(pi->osh->pdev, pi->pmecap_offset + PME_CSR_OFFSET,
				&w);

	return (w & PME_CSR_PME_STAT) == PME_CSR_PME_STAT;
}

/* Disable PME generation, clear the PME status bit if set
 */
void pcicore_pmeclr(void *pch)
{
	pcicore_info_t *pi = (pcicore_info_t *) pch;
	u32 w;

	if (!pcicore_pmecap(pi))
		return;

	pci_read_config_dword(pi->osh->pdev, pi->pmecap_offset + PME_CSR_OFFSET,
				&w);

	PCI_ERROR(("pcicore_pci_pmeclr PMECSR : 0x%x\n", w));

	/* PMESTAT is cleared by writing 1 to it */
	w &= ~(PME_CSR_PME_EN);

	pci_write_config_dword(pi->osh->pdev,
				pi->pmecap_offset + PME_CSR_OFFSET, w);
}

u32 pcie_lcreg(void *pch, u32 mask, u32 val)
{
	pcicore_info_t *pi = (pcicore_info_t *) pch;
	u8 offset;
	u32 tmpval;

	offset = pi->pciecap_lcreg_offset;
	if (!offset)
		return 0;

	/* set operation */
	if (mask)
		pci_write_config_dword(pi->osh->pdev, offset, val);

	pci_read_config_dword(pi->osh->pdev, offset, &tmpval);
	return tmpval;
}

u32
pcicore_pciereg(void *pch, u32 offset, u32 mask, u32 val, uint type)
{
	u32 reg_val = 0;
	pcicore_info_t *pi = (pcicore_info_t *) pch;
	sbpcieregs_t *pcieregs = pi->regs.pcieregs;
	struct osl_info *osh = pi->osh;

	if (mask) {
		PCI_ERROR(("PCIEREG: 0x%x writeval  0x%x\n", offset, val));
		pcie_writereg(osh, pcieregs, type, offset, val);
	}

	/* Should not read register 0x154 */
	if (pi->sih->buscorerev <= 5 && offset == PCIE_DLLP_PCIE11
	    && type == PCIE_PCIEREGS)
		return reg_val;

	reg_val = pcie_readreg(osh, pcieregs, type, offset);
	PCI_ERROR(("PCIEREG: 0x%x readval is 0x%x\n", offset, reg_val));

	return reg_val;
}

u32
pcicore_pcieserdesreg(void *pch, u32 mdioslave, u32 offset, u32 mask,
		      u32 val)
{
	u32 reg_val = 0;
	pcicore_info_t *pi = (pcicore_info_t *) pch;

	if (mask) {
		PCI_ERROR(("PCIEMDIOREG: 0x%x writeval  0x%x\n", offset, val));
		pcie_mdiowrite(pi, mdioslave, offset, val);
	}

	if (pcie_mdioread(pi, mdioslave, offset, &reg_val))
		reg_val = 0xFFFFFFFF;
	PCI_ERROR(("PCIEMDIOREG: dev 0x%x offset 0x%x read 0x%x\n", mdioslave,
		   offset, reg_val));

	return reg_val;
}
