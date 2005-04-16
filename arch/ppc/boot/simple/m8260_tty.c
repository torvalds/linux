/* Minimal serial functions needed to send messages out the serial
 * port on SMC1.
 */
#include <linux/types.h>
#include <asm/mpc8260.h>
#include <asm/cpm2.h>
#include <asm/immap_cpm2.h>

uint	no_print;
extern char	*params[];
extern int	nparams;
static		u_char	cons_hold[128], *sgptr;
static		int	cons_hold_cnt;

/* If defined, enables serial console.  The value (1 through 4)
 * should designate which SCC is used, but this isn't complete.  Only
 * SCC1 is known to work at this time.
 * We're only linked if SERIAL_CPM_CONSOLE=y, so we only need to test
 * SERIAL_CPM_SCC1.
 */
#ifdef CONFIG_SERIAL_CPM_SCC1
#define SCC_CONSOLE 1
#endif

unsigned long
serial_init(int ignored, bd_t *bd)
{
#ifdef SCC_CONSOLE
	volatile scc_t		*sccp;
	volatile scc_uart_t	*sup;
#else
	volatile smc_t		*sp;
	volatile smc_uart_t	*up;
#endif
	volatile cbd_t	*tbdf, *rbdf;
	volatile cpm2_map_t	*ip;
	volatile iop_cpm2_t	*io;
	volatile cpm_cpm2_t	*cp;
	uint	dpaddr, memaddr;

	ip = (cpm2_map_t *)CPM_MAP_ADDR;
	cp = &ip->im_cpm;
	io = &ip->im_ioport;

	/* Perform a reset.
	*/
	cp->cp_cpcr = (CPM_CR_RST | CPM_CR_FLG);

	/* Wait for it.
	*/
	while (cp->cp_cpcr & CPM_CR_FLG);

#ifdef CONFIG_ADS8260
	/* Enable the RS-232 transceivers.
	*/
	*(volatile uint *)(BCSR_ADDR + 4) &=
					~(BCSR1_RS232_EN1 | BCSR1_RS232_EN2);
#endif

#ifdef SCC_CONSOLE
	sccp = (scc_t *)&(ip->im_scc[SCC_CONSOLE-1]);
	sup = (scc_uart_t *)&ip->im_dprambase[PROFF_SCC1 + ((SCC_CONSOLE-1) << 8)];
	sccp->scc_sccm &= ~(UART_SCCM_TX | UART_SCCM_RX);
	sccp->scc_gsmrl &= ~(SCC_GSMRL_ENR | SCC_GSMRL_ENT);

	/* Use Port D for SCC1 instead of other functions.
	*/
	io->iop_ppard |= 0x00000003;
	io->iop_psord &= ~0x00000001;	/* Rx */
	io->iop_psord |= 0x00000002;	/* Tx */
	io->iop_pdird &= ~0x00000001;	/* Rx */
	io->iop_pdird |= 0x00000002;	/* Tx */

#else
	sp = (smc_t*)&(ip->im_smc[0]);
	*(ushort *)(&ip->im_dprambase[PROFF_SMC1_BASE]) = PROFF_SMC1;
	up = (smc_uart_t *)&ip->im_dprambase[PROFF_SMC1];

	/* Disable transmitter/receiver.
	*/
	sp->smc_smcmr &= ~(SMCMR_REN | SMCMR_TEN);

	/* Use Port D for SMC1 instead of other functions.
	*/
	io->iop_ppard |= 0x00c00000;
	io->iop_pdird |= 0x00400000;
	io->iop_pdird &= ~0x00800000;
	io->iop_psord &= ~0x00c00000;
#endif

	/* Allocate space for two buffer descriptors in the DP ram.
	 * For now, this address seems OK, but it may have to
	 * change with newer versions of the firmware.
	 */
	dpaddr = 0x0800;

	/* Grab a few bytes from the top of memory.
	 */
	memaddr = (bd->bi_memsize - 256) & ~15;

	/* Set the physical address of the host memory buffers in
	 * the buffer descriptors.
	 */
	rbdf = (cbd_t *)&ip->im_dprambase[dpaddr];
	rbdf->cbd_bufaddr = memaddr;
	rbdf->cbd_sc = 0;
	tbdf = rbdf + 1;
	tbdf->cbd_bufaddr = memaddr+128;
	tbdf->cbd_sc = 0;

	/* Set up the uart parameters in the parameter ram.
	*/
#ifdef SCC_CONSOLE
	sup->scc_genscc.scc_rbase = dpaddr;
	sup->scc_genscc.scc_tbase = dpaddr + sizeof(cbd_t);

	/* Set up the uart parameters in the
	 * parameter ram.
	 */
	sup->scc_genscc.scc_rfcr = CPMFCR_GBL | CPMFCR_EB;
	sup->scc_genscc.scc_tfcr = CPMFCR_GBL | CPMFCR_EB;

	sup->scc_genscc.scc_mrblr = 128;
	sup->scc_maxidl = 8;
	sup->scc_brkcr = 1;
	sup->scc_parec = 0;
	sup->scc_frmec = 0;
	sup->scc_nosec = 0;
	sup->scc_brkec = 0;
	sup->scc_uaddr1 = 0;
	sup->scc_uaddr2 = 0;
	sup->scc_toseq = 0;
	sup->scc_char1 = 0x8000;
	sup->scc_char2 = 0x8000;
	sup->scc_char3 = 0x8000;
	sup->scc_char4 = 0x8000;
	sup->scc_char5 = 0x8000;
	sup->scc_char6 = 0x8000;
	sup->scc_char7 = 0x8000;
	sup->scc_char8 = 0x8000;
	sup->scc_rccm = 0xc0ff;

	/* Send the CPM an initialize command.
	*/
	cp->cp_cpcr = mk_cr_cmd(CPM_CR_SCC1_PAGE, CPM_CR_SCC1_SBLOCK, 0,
			CPM_CR_INIT_TRX) | CPM_CR_FLG;
	while (cp->cp_cpcr & CPM_CR_FLG);

	/* Set UART mode, 8 bit, no parity, one stop.
	 * Enable receive and transmit.
	 */
	sccp->scc_gsmrh = 0;
	sccp->scc_gsmrl =
		(SCC_GSMRL_MODE_UART | SCC_GSMRL_TDCR_16 | SCC_GSMRL_RDCR_16);

	/* Disable all interrupts and clear all pending
	 * events.
	 */
	sccp->scc_sccm = 0;
	sccp->scc_scce = 0xffff;
	sccp->scc_dsr = 0x7e7e;
	sccp->scc_psmr = 0x3000;

	/* Wire BRG1 to SCC1.  The console driver will take care of
	 * others.
	 */
	ip->im_cpmux.cmx_scr = 0;
#else
	up->smc_rbase = dpaddr;
	up->smc_tbase = dpaddr+sizeof(cbd_t);
	up->smc_rfcr = CPMFCR_EB;
	up->smc_tfcr = CPMFCR_EB;
	up->smc_brklen = 0;
	up->smc_brkec = 0;
	up->smc_brkcr = 0;
	up->smc_mrblr = 128;
	up->smc_maxidl = 8;

	/* Set UART mode, 8 bit, no parity, one stop.
	 * Enable receive and transmit.
	 */
	sp->smc_smcmr = smcr_mk_clen(9) |  SMCMR_SM_UART;

	/* Mask all interrupts and remove anything pending.
	*/
	sp->smc_smcm = 0;
	sp->smc_smce = 0xff;

	/* Set up the baud rate generator.
	 */
	ip->im_cpmux.cmx_smr = 0;
#endif

	/* The baud rate divisor needs to be coordinated with clk_8260().
	*/
	ip->im_brgc1 =
		(((bd->bi_brgfreq/16) / bd->bi_baudrate) << 1) |
								CPM_BRG_EN;

	/* Make the first buffer the only buffer.
	*/
	tbdf->cbd_sc |= BD_SC_WRAP;
	rbdf->cbd_sc |= BD_SC_EMPTY | BD_SC_WRAP;

	/* Initialize Tx/Rx parameters.
	*/
#ifdef SCC_CONSOLE
	sccp->scc_gsmrl |= (SCC_GSMRL_ENR | SCC_GSMRL_ENT);
#else
	cp->cp_cpcr = mk_cr_cmd(CPM_CR_SMC1_PAGE, CPM_CR_SMC1_SBLOCK, 0, CPM_CR_INIT_TRX) | CPM_CR_FLG;
	while (cp->cp_cpcr & CPM_CR_FLG);

	/* Enable transmitter/receiver.
	*/
	sp->smc_smcmr |= SMCMR_REN | SMCMR_TEN;
#endif

	/* This is ignored.
	*/
	return 0;
}

int
serial_readbuf(u_char *cbuf)
{
	volatile cbd_t		*rbdf;
	volatile char		*buf;
#ifdef SCC_CONSOLE
	volatile scc_uart_t	*sup;
#else
	volatile smc_uart_t	*up;
#endif
	volatile cpm2_map_t	*ip;
	int	i, nc;

	ip = (cpm2_map_t *)CPM_MAP_ADDR;

#ifdef SCC_CONSOLE
	sup = (scc_uart_t *)&ip->im_dprambase[PROFF_SCC1 + ((SCC_CONSOLE-1) << 8)];
	rbdf = (cbd_t *)&ip->im_dprambase[sup->scc_genscc.scc_rbase];
#else
	up = (smc_uart_t *)&(ip->im_dprambase[PROFF_SMC1]);
	rbdf = (cbd_t *)&ip->im_dprambase[up->smc_rbase];
#endif

	/* Wait for character to show up.
	*/
	buf = (char *)rbdf->cbd_bufaddr;
	while (rbdf->cbd_sc & BD_SC_EMPTY);
	nc = rbdf->cbd_datlen;
	for (i=0; i<nc; i++)
		*cbuf++ = *buf++;
	rbdf->cbd_sc |= BD_SC_EMPTY;

	return(nc);
}

void
serial_putc(void *ignored, const char c)
{
	volatile cbd_t		*tbdf;
	volatile char		*buf;
#ifdef SCC_CONSOLE
	volatile scc_uart_t	*sup;
#else
	volatile smc_uart_t	*up;
#endif
	volatile cpm2_map_t	*ip;

	ip = (cpm2_map_t *)CPM_MAP_ADDR;
#ifdef SCC_CONSOLE
	sup = (scc_uart_t *)&ip->im_dprambase[PROFF_SCC1 + ((SCC_CONSOLE-1) << 8)];
	tbdf = (cbd_t *)&ip->im_dprambase[sup->scc_genscc.scc_tbase];
#else
	up = (smc_uart_t *)&(ip->im_dprambase[PROFF_SMC1]);
	tbdf = (cbd_t *)&ip->im_dprambase[up->smc_tbase];
#endif

	/* Wait for last character to go.
	*/
	buf = (char *)tbdf->cbd_bufaddr;
	while (tbdf->cbd_sc & BD_SC_READY);

	*buf = c;
	tbdf->cbd_datlen = 1;
	tbdf->cbd_sc |= BD_SC_READY;
}

char
serial_getc(void *ignored)
{
	char	c;

	if (cons_hold_cnt <= 0) {
		cons_hold_cnt = serial_readbuf(cons_hold);
		sgptr = cons_hold;
	}
	c = *sgptr++;
	cons_hold_cnt--;

	return(c);
}

int
serial_tstc(void *ignored)
{
	volatile cbd_t		*rbdf;
#ifdef SCC_CONSOLE
	volatile scc_uart_t	*sup;
#else
	volatile smc_uart_t	*up;
#endif
	volatile cpm2_map_t	*ip;

	ip = (cpm2_map_t *)CPM_MAP_ADDR;
#ifdef SCC_CONSOLE
	sup = (scc_uart_t *)&ip->im_dprambase[PROFF_SCC1 + ((SCC_CONSOLE-1) << 8)];
	rbdf = (cbd_t *)&ip->im_dprambase[sup->scc_genscc.scc_rbase];
#else
	up = (smc_uart_t *)&(ip->im_dprambase[PROFF_SMC1]);
	rbdf = (cbd_t *)&ip->im_dprambase[up->smc_rbase];
#endif

	return(!(rbdf->cbd_sc & BD_SC_EMPTY));
}
