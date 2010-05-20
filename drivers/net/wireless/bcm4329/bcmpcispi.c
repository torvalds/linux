/*
 * Broadcom SPI over PCI-SPI Host Controller, low-level hardware driver
 *
 * Copyright (C) 1999-2009, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: bcmpcispi.c,v 1.22.2.4.4.5 2008/07/09 21:23:30 Exp $
 */

#include <typedefs.h>
#include <bcmutils.h>

#include <sdio.h>		/* SDIO Specs */
#include <bcmsdbus.h>		/* bcmsdh to/from specific controller APIs */
#include <sdiovar.h>		/* to get msglevel bit values */

#include <pcicfg.h>
#include <bcmsdspi.h>
#include <bcmspi.h>
#include <bcmpcispi.h>		/* BRCM PCI-SPI Host Controller Register definitions */


/* ndis_osl.h needs to do a runtime check of the osh to map
 * R_REG/W_REG to bus specific access similar to linux_osl.h.
 * Until then...
 */
/* linux */

#define SPIPCI_RREG R_REG
#define SPIPCI_WREG W_REG


#define	SPIPCI_ANDREG(osh, r, v) SPIPCI_WREG(osh, (r), (SPIPCI_RREG(osh, r) & (v)))
#define	SPIPCI_ORREG(osh, r, v)	SPIPCI_WREG(osh, (r), (SPIPCI_RREG(osh, r) | (v)))


int bcmpcispi_dump = 0;		/* Set to dump complete trace of all SPI bus transactions */

typedef struct spih_info_ {
	uint		bar0;		/* BAR0 of PCI Card */
	uint		bar1;		/* BAR1 of PCI Card */
	osl_t 		*osh;		/* osh handle */
	spih_pciregs_t	*pciregs;	/* PCI Core Registers */
	spih_regs_t	*regs;		/* SPI Controller Registers */
	uint8		rev;		/* PCI Card Revision ID */
} spih_info_t;


/* Attach to PCI-SPI Host Controller Hardware */
bool
spi_hw_attach(sdioh_info_t *sd)
{
	osl_t *osh;
	spih_info_t *si;

	sd_trace(("%s: enter\n", __FUNCTION__));

	osh = sd->osh;

	if ((si = (spih_info_t *)MALLOC(osh, sizeof(spih_info_t))) == NULL) {
		sd_err(("%s: out of memory, malloced %d bytes\n", __FUNCTION__, MALLOCED(osh)));
		return FALSE;
	}

	bzero(si, sizeof(spih_info_t));

	sd->controller = si;

	si->osh = sd->osh;
	si->rev = OSL_PCI_READ_CONFIG(sd->osh, PCI_CFG_REV, 4) & 0xFF;

	if (si->rev < 3) {
		sd_err(("Host controller %d not supported, please upgrade to rev >= 3\n", si->rev));
		MFREE(osh, si, sizeof(spih_info_t));
		return (FALSE);
	}

	sd_err(("Attaching to Generic PCI SPI Host Controller Rev %d\n", si->rev));

	/* FPGA Revision < 3 not supported by driver anymore. */
	ASSERT(si->rev >= 3);

	si->bar0 = sd->bar0;

	/* Rev < 10 PciSpiHost has 2 BARs:
	 *    BAR0 = PCI Core Registers
	 *    BAR1 = PciSpiHost Registers (all other cores on backplane)
	 *
	 * Rev 10 and up use a different PCI core which only has a single
	 * BAR0 which contains the PciSpiHost Registers.
	 */
	if (si->rev < 10) {
		si->pciregs = (spih_pciregs_t *)spi_reg_map(osh,
		                                              (uintptr)si->bar0,
		                                              sizeof(spih_pciregs_t));
		sd_err(("Mapped PCI Core regs to BAR0 at %p\n", si->pciregs));

		si->bar1 = OSL_PCI_READ_CONFIG(sd->osh, PCI_CFG_BAR1, 4);
		si->regs = (spih_regs_t *)spi_reg_map(osh,
		                                        (uintptr)si->bar1,
		                                        sizeof(spih_regs_t));
		sd_err(("Mapped SPI Controller regs to BAR1 at %p\n", si->regs));
	} else {
		si->regs = (spih_regs_t *)spi_reg_map(osh,
		                                              (uintptr)si->bar0,
		                                              sizeof(spih_regs_t));
		sd_err(("Mapped SPI Controller regs to BAR0 at %p\n", si->regs));
		si->pciregs = NULL;
	}
	/* Enable SPI Controller, 16.67MHz SPI Clock */
	SPIPCI_WREG(osh, &si->regs->spih_ctrl, 0x000000d1);

	/* Set extended feature register to defaults */
	SPIPCI_WREG(osh, &si->regs->spih_ext, 0x00000000);

	/* Set GPIO CS# High (de-asserted) */
	SPIPCI_WREG(osh, &si->regs->spih_gpio_data, SPIH_CS);

	/* set GPIO[0] to output for CS# */
	/* set GPIO[1] to output for power control */
	/* set GPIO[2] to input for card detect */
	SPIPCI_WREG(osh, &si->regs->spih_gpio_ctrl, (SPIH_CS | SPIH_SLOT_POWER));

	/* Clear out the Read FIFO in case there is any stuff left in there from a previous run. */
	while ((SPIPCI_RREG(osh, &si->regs->spih_stat) & SPIH_RFEMPTY) == 0) {
		SPIPCI_RREG(osh, &si->regs->spih_data);
	}

	/* Wait for power to stabilize to the SDIO Card (100msec was insufficient) */
	OSL_DELAY(250000);

	/* Check card detect on FPGA Revision >= 4 */
	if (si->rev >= 4) {
		if (SPIPCI_RREG(osh, &si->regs->spih_gpio_data) & SPIH_CARD_DETECT) {
			sd_err(("%s: no card detected in SD slot\n", __FUNCTION__));
			spi_reg_unmap(osh, (uintptr)si->regs, sizeof(spih_regs_t));
			if (si->pciregs) {
				spi_reg_unmap(osh, (uintptr)si->pciregs, sizeof(spih_pciregs_t));
			}
			MFREE(osh, si, sizeof(spih_info_t));
			return FALSE;
		}
	}

	/* Interrupts are level sensitive */
	SPIPCI_WREG(osh, &si->regs->spih_int_edge, 0x80000000);

	/* Interrupts are active low. */
	SPIPCI_WREG(osh, &si->regs->spih_int_pol, 0x40000004);

	/* Enable interrupts through PCI Core. */
	if (si->pciregs) {
		SPIPCI_WREG(osh, &si->pciregs->ICR, PCI_INT_PROP_EN);
	}

	sd_trace(("%s: exit\n", __FUNCTION__));
	return TRUE;
}

/* Detach and return PCI-SPI Hardware to unconfigured state */
bool
spi_hw_detach(sdioh_info_t *sd)
{
	spih_info_t *si = (spih_info_t *)sd->controller;
	osl_t *osh = si->osh;
	spih_regs_t *regs = si->regs;
	spih_pciregs_t *pciregs = si->pciregs;

	sd_trace(("%s: enter\n", __FUNCTION__));

	SPIPCI_WREG(osh, &regs->spih_ctrl, 0x00000010);
	SPIPCI_WREG(osh, &regs->spih_gpio_ctrl, 0x00000000);	/* Disable GPIO for CS# */
	SPIPCI_WREG(osh, &regs->spih_int_mask, 0x00000000);	/* Clear Intmask */
	SPIPCI_WREG(osh, &regs->spih_hex_disp, 0x0000DEAF);
	SPIPCI_WREG(osh, &regs->spih_int_edge, 0x00000000);
	SPIPCI_WREG(osh, &regs->spih_int_pol, 0x00000000);
	SPIPCI_WREG(osh, &regs->spih_hex_disp, 0x0000DEAD);

	/* Disable interrupts through PCI Core. */
	if (si->pciregs) {
		SPIPCI_WREG(osh, &pciregs->ICR, 0x00000000);
		spi_reg_unmap(osh, (uintptr)pciregs, sizeof(spih_pciregs_t));
	}
	spi_reg_unmap(osh, (uintptr)regs, sizeof(spih_regs_t));

	MFREE(osh, si, sizeof(spih_info_t));

	sd->controller = NULL;

	sd_trace(("%s: exit\n", __FUNCTION__));
	return TRUE;
}

/* Switch between internal (PCI) and external clock oscillator */
static bool
sdspi_switch_clock(sdioh_info_t *sd, bool ext_clk)
{
	spih_info_t *si = (spih_info_t *)sd->controller;
	osl_t *osh = si->osh;
	spih_regs_t *regs = si->regs;

	/* Switch to desired clock, and reset the PLL. */
	SPIPCI_WREG(osh, &regs->spih_pll_ctrl, ext_clk ? SPIH_EXT_CLK : 0);

	SPINWAIT(((SPIPCI_RREG(osh, &regs->spih_pll_status) & SPIH_PLL_LOCKED)
	          != SPIH_PLL_LOCKED), 1000);
	if ((SPIPCI_RREG(osh, &regs->spih_pll_status) & SPIH_PLL_LOCKED) != SPIH_PLL_LOCKED) {
		sd_err(("%s: timeout waiting for PLL to lock\n", __FUNCTION__));
		return (FALSE);
	}
	return (TRUE);

}

/* Configure PCI-SPI Host Controller's SPI Clock rate as a divisor into the
 * base clock rate.  The base clock is either the PCI Clock (33MHz) or the
 * external clock oscillator at U17 on the PciSpiHost.
 */
bool
spi_start_clock(sdioh_info_t *sd, uint16 div)
{
	spih_info_t *si = (spih_info_t *)sd->controller;
	osl_t *osh = si->osh;
	spih_regs_t *regs = si->regs;
	uint32 t, espr, disp;
	uint32 disp_xtal_freq;
	bool	ext_clock = FALSE;
	char disp_string[5];

	if (div > 2048) {
		sd_err(("%s: divisor %d too large; using max of 2048\n", __FUNCTION__, div));
		div = 2048;
	} else if (div & (div - 1)) {	/* Not a power of 2? */
		/* Round up to a power of 2 */
		while ((div + 1) & div)
			div |= div >> 1;
		div++;
	}

	/* For FPGA Rev >= 5, the use of an external clock oscillator is supported.
	 * If the oscillator is populated, use it to provide the SPI base clock,
	 * otherwise, default to the PCI clock as the SPI base clock.
	 */
	if (si->rev >= 5) {
		uint32 clk_tick;
		/* Enable the External Clock Oscillator as PLL clock source. */
		if (!sdspi_switch_clock(sd, TRUE)) {
			sd_err(("%s: error switching to external clock\n", __FUNCTION__));
		}

		/* Check to make sure the external clock is running.  If not, then it
		 * is not populated on the card, so we will default to the PCI clock.
		 */
		clk_tick = SPIPCI_RREG(osh, &regs->spih_clk_count);
		if (clk_tick == SPIPCI_RREG(osh, &regs->spih_clk_count)) {

			/* Switch back to the PCI clock as the clock source. */
			if (!sdspi_switch_clock(sd, FALSE)) {
				sd_err(("%s: error switching to external clock\n", __FUNCTION__));
			}
		} else {
			ext_clock = TRUE;
		}
	}

	/* Hack to allow hot-swapping oscillators:
	 * 1. Force PCI clock as clock source, using sd_divisor of 0.
	 * 2. Swap oscillator
	 * 3. Set desired sd_divisor (will switch to external oscillator as clock source.
	 */
	if (div == 0) {
		ext_clock = FALSE;
		div = 2;

		/* Select PCI clock as the clock source. */
		if (!sdspi_switch_clock(sd, FALSE)) {
			sd_err(("%s: error switching to external clock\n", __FUNCTION__));
		}

		sd_err(("%s: Ok to hot-swap oscillators.\n", __FUNCTION__));
	}

	/* If using the external oscillator, read the clock frequency from the controller
	 * The value read is in units of 10000Hz, and it's not a nice round number because
	 * it is calculated by the FPGA.  So to make up for that, we round it off.
	 */
	if (ext_clock == TRUE) {
		uint32 xtal_freq;

		OSL_DELAY(1000);
		xtal_freq = SPIPCI_RREG(osh, &regs->spih_xtal_freq) * 10000;

		sd_info(("%s: Oscillator is %dHz\n", __FUNCTION__, xtal_freq));


		disp_xtal_freq = xtal_freq / 10000;

		/* Round it off to a nice number. */
		if ((disp_xtal_freq % 100) > 50) {
			disp_xtal_freq += 100;
		}

		disp_xtal_freq = (disp_xtal_freq / 100) * 100;
	} else {
		sd_err(("%s: no external oscillator installed, using PCI clock.\n", __FUNCTION__));
		disp_xtal_freq = 3333;
	}

	/* Convert the SPI Clock frequency to BCD format. */
	sprintf(disp_string, "%04d", disp_xtal_freq / div);

	disp  = (disp_string[0] - '0') << 12;
	disp |= (disp_string[1] - '0') << 8;
	disp |= (disp_string[2] - '0') << 4;
	disp |= (disp_string[3] - '0');

	/* Select the correct ESPR register value based on the divisor. */
	switch (div) {
		case 1:		espr = 0x0; break;
		case 2:		espr = 0x1; break;
		case 4:		espr = 0x2; break;
		case 8:		espr = 0x5; break;
		case 16:	espr = 0x3; break;
		case 32:	espr = 0x4; break;
		case 64:	espr = 0x6; break;
		case 128:	espr = 0x7; break;
		case 256:	espr = 0x8; break;
		case 512:	espr = 0x9; break;
		case 1024:	espr = 0xa; break;
		case 2048:	espr = 0xb; break;
		default:	espr = 0x0; ASSERT(0); break;
	}

	t = SPIPCI_RREG(osh, &regs->spih_ctrl);
	t &= ~3;
	t |= espr & 3;
	SPIPCI_WREG(osh, &regs->spih_ctrl, t);

	t = SPIPCI_RREG(osh, &regs->spih_ext);
	t &= ~3;
	t |= (espr >> 2) & 3;
	SPIPCI_WREG(osh, &regs->spih_ext, t);

	SPIPCI_WREG(osh, &regs->spih_hex_disp, disp);

	/* For Rev 8, writing to the PLL_CTRL register resets
	 * the PLL, and it can re-acquire in 200uS.  For
	 * Rev 7 and older, we use a software delay to allow
	 * the PLL to re-acquire, which takes more than 2mS.
	 */
	if (si->rev < 8) {
		/* Wait for clock to settle. */
		OSL_DELAY(5000);
	}

	sd_info(("%s: SPI_CTRL=0x%08x SPI_EXT=0x%08x\n",
	         __FUNCTION__,
	         SPIPCI_RREG(osh, &regs->spih_ctrl),
	         SPIPCI_RREG(osh, &regs->spih_ext)));

	return TRUE;
}

/* Configure PCI-SPI Host Controller High-Speed Clocking mode setting */
bool
spi_controller_highspeed_mode(sdioh_info_t *sd, bool hsmode)
{
	spih_info_t *si = (spih_info_t *)sd->controller;
	osl_t *osh = si->osh;
	spih_regs_t *regs = si->regs;

	if (si->rev >= 10) {
		if (hsmode) {
			SPIPCI_ORREG(osh, &regs->spih_ext, 0x10);
		} else {
			SPIPCI_ANDREG(osh, &regs->spih_ext, ~0x10);
		}
	}

	return TRUE;
}

/* Disable device interrupt */
void
spi_devintr_off(sdioh_info_t *sd)
{
	spih_info_t *si = (spih_info_t *)sd->controller;
	osl_t *osh = si->osh;
	spih_regs_t *regs = si->regs;

	sd_trace(("%s: %d\n", __FUNCTION__, sd->use_client_ints));
	if (sd->use_client_ints) {
		sd->intmask &= ~SPIH_DEV_INTR;
		SPIPCI_WREG(osh, &regs->spih_int_mask, sd->intmask);	/* Clear Intmask */
	}
}

/* Enable device interrupt */
void
spi_devintr_on(sdioh_info_t *sd)
{
	spih_info_t *si = (spih_info_t *)sd->controller;
	osl_t *osh = si->osh;
	spih_regs_t *regs = si->regs;

	ASSERT(sd->lockcount == 0);
	sd_trace(("%s: %d\n", __FUNCTION__, sd->use_client_ints));
	if (sd->use_client_ints) {
		if (SPIPCI_RREG(osh, &regs->spih_ctrl) & 0x02) {
			/* Ack in case one was pending but is no longer... */
			SPIPCI_WREG(osh, &regs->spih_int_status, SPIH_DEV_INTR);
		}
		sd->intmask |= SPIH_DEV_INTR;
		/* Set device intr in Intmask */
		SPIPCI_WREG(osh, &regs->spih_int_mask, sd->intmask);
	}
}

/* Check to see if an interrupt belongs to the PCI-SPI Host or a SPI Device */
bool
spi_check_client_intr(sdioh_info_t *sd, int *is_dev_intr)
{
	spih_info_t *si = (spih_info_t *)sd->controller;
	osl_t *osh = si->osh;
	spih_regs_t *regs = si->regs;
	bool ours = FALSE;

	uint32 raw_int, cur_int;
	ASSERT(sd);

	if (is_dev_intr)
		*is_dev_intr = FALSE;
	raw_int = SPIPCI_RREG(osh, &regs->spih_int_status);
	cur_int = raw_int & sd->intmask;
	if (cur_int & SPIH_DEV_INTR) {
		if (sd->client_intr_enabled && sd->use_client_ints) {
			sd->intrcount++;
			ASSERT(sd->intr_handler);
			ASSERT(sd->intr_handler_arg);
			(sd->intr_handler)(sd->intr_handler_arg);
			if (is_dev_intr)
				*is_dev_intr = TRUE;
		} else {
			sd_trace(("%s: Not ready for intr: enabled %d, handler 0x%p\n",
			        __FUNCTION__, sd->client_intr_enabled, sd->intr_handler));
		}
		SPIPCI_WREG(osh, &regs->spih_int_status, SPIH_DEV_INTR);
		SPIPCI_RREG(osh, &regs->spih_int_status);
		ours = TRUE;
	} else if (cur_int & SPIH_CTLR_INTR) {
		/* Interrupt is from SPI FIFO... just clear and ack it... */
		sd_trace(("%s: SPI CTLR interrupt: raw_int 0x%08x cur_int 0x%08x\n",
		          __FUNCTION__, raw_int, cur_int));

		/* Clear the interrupt in the SPI_STAT register */
		SPIPCI_WREG(osh, &regs->spih_stat, 0x00000080);

		/* Ack the interrupt in the interrupt controller */
		SPIPCI_WREG(osh, &regs->spih_int_status, SPIH_CTLR_INTR);
		SPIPCI_RREG(osh, &regs->spih_int_status);

		ours = TRUE;
	} else if (cur_int & SPIH_WFIFO_INTR) {
		sd_trace(("%s: SPI WR FIFO Empty interrupt: raw_int 0x%08x cur_int 0x%08x\n",
		          __FUNCTION__, raw_int, cur_int));

		/* Disable the FIFO Empty Interrupt */
		sd->intmask &= ~SPIH_WFIFO_INTR;
		SPIPCI_WREG(osh, &regs->spih_int_mask, sd->intmask);

		sd->local_intrcount++;
		sd->got_hcint = TRUE;
		ours = TRUE;
	} else {
		/* Not an error: can share interrupts... */
		sd_trace(("%s: Not my interrupt: raw_int 0x%08x cur_int 0x%08x\n",
		          __FUNCTION__, raw_int, cur_int));
		ours = FALSE;
	}

	return ours;
}

static void
hexdump(char *pfx, unsigned char *msg, int msglen)
{
	int i, col;
	char buf[80];

	ASSERT(strlen(pfx) + 49 <= sizeof(buf));

	col = 0;

	for (i = 0; i < msglen; i++, col++) {
		if (col % 16 == 0)
			strcpy(buf, pfx);
		sprintf(buf + strlen(buf), "%02x", msg[i]);
		if ((col + 1) % 16 == 0)
			printf("%s\n", buf);
		else
			sprintf(buf + strlen(buf), " ");
	}

	if (col % 16 != 0)
		printf("%s\n", buf);
}

/* Send/Receive an SPI Packet */
void
spi_sendrecv(sdioh_info_t *sd, uint8 *msg_out, uint8 *msg_in, int msglen)
{
	spih_info_t *si = (spih_info_t *)sd->controller;
	osl_t *osh = si->osh;
	spih_regs_t *regs = si->regs;
	uint32 count;
	uint32 spi_data_out;
	uint32 spi_data_in;
	bool yield;

	sd_trace(("%s: enter\n", __FUNCTION__));

	if (bcmpcispi_dump) {
		printf("SENDRECV(len=%d)\n", msglen);
		hexdump(" OUT: ", msg_out, msglen);
	}

#ifdef BCMSDYIELD
	/* Only yield the CPU and wait for interrupt on Rev 8 and newer FPGA images. */
	yield = ((msglen > 500) && (si->rev >= 8));
#else
	yield = FALSE;
#endif /* BCMSDYIELD */

	ASSERT(msglen % 4 == 0);


	SPIPCI_ANDREG(osh, &regs->spih_gpio_data, ~SPIH_CS);	/* Set GPIO CS# Low (asserted) */

	for (count = 0; count < (uint32)msglen/4; count++) {
		spi_data_out = ((uint32)((uint32 *)msg_out)[count]);
		SPIPCI_WREG(osh, &regs->spih_data, spi_data_out);
	}

#ifdef BCMSDYIELD
	if (yield) {
		/* Ack the interrupt in the interrupt controller */
		SPIPCI_WREG(osh, &regs->spih_int_status, SPIH_WFIFO_INTR);
		SPIPCI_RREG(osh, &regs->spih_int_status);

		/* Enable the FIFO Empty Interrupt */
		sd->intmask |= SPIH_WFIFO_INTR;
		sd->got_hcint = FALSE;
		SPIPCI_WREG(osh, &regs->spih_int_mask, sd->intmask);

	}
#endif /* BCMSDYIELD */

	/* Wait for write fifo to empty... */
	SPIPCI_ANDREG(osh, &regs->spih_gpio_data, ~0x00000020);	/* Set GPIO 5 Low */

	if (yield) {
		ASSERT((SPIPCI_RREG(sd->osh, &regs->spih_stat) & SPIH_WFEMPTY) == 0);
	}

	spi_waitbits(sd, yield);
	SPIPCI_ORREG(osh, &regs->spih_gpio_data, 0x00000020);	/* Set GPIO 5 High (de-asserted) */

	for (count = 0; count < (uint32)msglen/4; count++) {
		spi_data_in = SPIPCI_RREG(osh, &regs->spih_data);
		((uint32 *)msg_in)[count] = spi_data_in;
	}

	/* Set GPIO CS# High (de-asserted) */
	SPIPCI_ORREG(osh, &regs->spih_gpio_data, SPIH_CS);

	if (bcmpcispi_dump) {
		hexdump(" IN : ", msg_in, msglen);
	}
}

void
spi_spinbits(sdioh_info_t *sd)
{
	spih_info_t *si = (spih_info_t *)sd->controller;
	osl_t *osh = si->osh;
	spih_regs_t *regs = si->regs;
	uint spin_count; /* Spin loop bound check */

	spin_count = 0;
	while ((SPIPCI_RREG(sd->osh, &regs->spih_stat) & SPIH_WFEMPTY) == 0) {
		if (spin_count > SPI_SPIN_BOUND) {
			ASSERT(FALSE); /* Spin bound exceeded */
		}
		spin_count++;
	}
	spin_count = 0;
	/* Wait for SPI Transfer state machine to return to IDLE state.
	 * The state bits are only implemented in Rev >= 5 FPGA.  These
	 * bits are hardwired to 00 for Rev < 5, so this check doesn't cause
	 * any problems.
	 */
	while ((SPIPCI_RREG(osh, &regs->spih_stat) & SPIH_STATE_MASK) != 0) {
		if (spin_count > SPI_SPIN_BOUND) {
			ASSERT(FALSE);
		}
		spin_count++;
	}
}
