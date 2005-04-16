/* Minimal support functions to read configuration from IIC EEPROMS
 * on MPC8xx boards.  Originally written for RPGC RPX-Lite.
 * Dan Malek (dmalek@jlc.net).
 */
#include <linux/types.h>
#include <asm/uaccess.h>
#include <asm/mpc8xx.h>
#include <asm/commproc.h>


/* IIC functions.
 * These are just the basic master read/write operations so we can
 * examine serial EEPROM.
 */
void	iic_read(uint devaddr, u_char *buf, uint offset, uint count);

static	int	iic_init_done;

static void
iic_init(void)
{
	volatile iic_t *iip;
	volatile i2c8xx_t *i2c;
	volatile cpm8xx_t	*cp;
	volatile immap_t	*immap;
	uint	dpaddr;

	immap = (immap_t *)IMAP_ADDR;
	cp = (cpm8xx_t *)&(immap->im_cpm);

	/* Reset the CPM.  This is necessary on the 860 processors
	 * that may have started the SCC1 ethernet without relocating
	 * the IIC.
	 * This also stops the Ethernet in case we were loaded by a
	 * BOOTP rom monitor.
	 */
	cp->cp_cpcr = (CPM_CR_RST | CPM_CR_FLG);

	/* Wait for it.
	*/
	while (cp->cp_cpcr & (CPM_CR_RST | CPM_CR_FLG));

	/* Remove any microcode patches.  We will install our own
	 * later.
	 */
	cp->cp_cpmcr1 = 0;
	cp->cp_cpmcr2 = 0;
	cp->cp_cpmcr3 = 0;
	cp->cp_cpmcr4 = 0;
	cp->cp_rccr = 0;

	iip = (iic_t *)&cp->cp_dparam[PROFF_IIC];
	i2c = (i2c8xx_t *)&(immap->im_i2c);

	/* Initialize Port B IIC pins.
	*/
	cp->cp_pbpar |= 0x00000030;
	cp->cp_pbdir |= 0x00000030;
	cp->cp_pbodr |= 0x00000030;

	/* Initialize the parameter ram.
	*/

	/* Allocate space for a two transmit and one receive buffer
	 * descriptor in the DP ram.
	 * For now, this address seems OK, but it may have to
	 * change with newer versions of the firmware.
	 */
	dpaddr = 0x0840;

	/* Set up the IIC parameters in the parameter ram.
	*/
	iip->iic_tbase = dpaddr;
	iip->iic_rbase = dpaddr + (2 * sizeof(cbd_t));

	iip->iic_tfcr = SMC_EB;
	iip->iic_rfcr = SMC_EB;

	/* This should really be done by the reader/writer.
	*/
	iip->iic_mrblr = 128;

	/* Initialize Tx/Rx parameters.
	*/
	cp->cp_cpcr = mk_cr_cmd(CPM_CR_CH_I2C, CPM_CR_INIT_TRX) | CPM_CR_FLG;
	while (cp->cp_cpcr & CPM_CR_FLG);

	/* Select an arbitrary address.  Just make sure it is unique.
	*/
	i2c->i2c_i2add = 0x34;

	/* Make clock run maximum slow.
	*/
	i2c->i2c_i2brg = 7;

	/* Disable interrupts.
	*/
	i2c->i2c_i2cmr = 0;
	i2c->i2c_i2cer = 0xff;

	/* Enable SDMA.
	*/
	immap->im_siu_conf.sc_sdcr = 1;

	iic_init_done = 1;
}

/* Read from IIC.
 * Caller provides device address, memory buffer, and byte count.
 */
static	u_char	iitemp[32];

void
iic_read(uint devaddr, u_char *buf, uint offset, uint count)
{
	volatile iic_t		*iip;
	volatile i2c8xx_t	*i2c;
	volatile cbd_t		*tbdf, *rbdf;
	volatile cpm8xx_t	*cp;
	volatile immap_t	*immap;
	u_char			*tb;
	uint			temp;

	/* If the interface has not been initialized, do that now.
	*/
	if (!iic_init_done)
		iic_init();

	immap = (immap_t *)IMAP_ADDR;
	cp = (cpm8xx_t *)&(immap->im_cpm);

	iip = (iic_t *)&cp->cp_dparam[PROFF_IIC];
	i2c = (i2c8xx_t *)&(immap->im_i2c);

	tbdf = (cbd_t *)&cp->cp_dpmem[iip->iic_tbase];
	rbdf = (cbd_t *)&cp->cp_dpmem[iip->iic_rbase];

	/* Send a "dummy write" operation.  This is a write request with
	 * only the offset sent, followed by another start condition.
	 * This will ensure we start reading from the first location
	 * of the EEPROM.
	 */
	tb = iitemp;
	tb = (u_char *)(((uint)tb + 15) & ~15);
	tbdf->cbd_bufaddr = (int)tb;
	*tb = devaddr & 0xfe;	/* Device address */
	*(tb+1) = offset;		/* Offset */
	tbdf->cbd_datlen = 2;		/* Length */
	tbdf->cbd_sc =
	      BD_SC_READY | BD_SC_LAST | BD_SC_WRAP | BD_IIC_START;

	i2c->i2c_i2mod = 1;	/* Enable */
	i2c->i2c_i2cer = 0xff;
	i2c->i2c_i2com = 0x81;	/* Start master */

	/* Wait for IIC transfer.
	*/
#if 0
	while ((i2c->i2c_i2cer & 3) == 0);

	if (tbdf->cbd_sc & BD_SC_READY)
		printf("IIC ra complete but tbuf ready\n");
#else
	temp = 10000000;
	while ((tbdf->cbd_sc & BD_SC_READY) && (temp != 0))
		temp--;
#if 0
	/* We can't do this...there is no serial port yet!
	*/
	if (temp == 0) {
		printf("Timeout reading EEPROM\n");
		return;
	}
#endif
#endif
	
	/* Chip errata, clear enable.
	*/
	i2c->i2c_i2mod = 0;

	/* To read, we need an empty buffer of the proper length.
	 * All that is used is the first byte for address, the remainder
	 * is just used for timing (and doesn't really have to exist).
	 */
	tbdf->cbd_bufaddr = (int)tb;
	*tb = devaddr | 1;	/* Device address */
	rbdf->cbd_bufaddr = (uint)buf;		/* Desination buffer */
	tbdf->cbd_datlen = rbdf->cbd_datlen = count + 1;	/* Length */
	tbdf->cbd_sc = BD_SC_READY | BD_SC_LAST | BD_SC_WRAP | BD_IIC_START;
	rbdf->cbd_sc = BD_SC_EMPTY | BD_SC_WRAP;

	/* Chip bug, set enable here.
	*/
	i2c->i2c_i2mod = 1;	/* Enable */
	i2c->i2c_i2cer = 0xff;
	i2c->i2c_i2com = 0x81;	/* Start master */

	/* Wait for IIC transfer.
	*/
#if 0
	while ((i2c->i2c_i2cer & 1) == 0);

	if (rbdf->cbd_sc & BD_SC_EMPTY)
		printf("IIC read complete but rbuf empty\n");
#else
	temp = 10000000;
	while ((tbdf->cbd_sc & BD_SC_READY) && (temp != 0))
		temp--;
#endif
	
	/* Chip errata, clear enable.
	*/
	i2c->i2c_i2mod = 0;
}
