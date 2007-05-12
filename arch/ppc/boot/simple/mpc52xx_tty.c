/*
 * Minimal serial functions needed to send messages out a MPC52xx
 * Programmable Serial Controller (PSC).
 *
 * Author: Dale Farnsworth <dfarnsworth@mvista.com>
 *
 * 2003-2004 (c) MontaVista, Software, Inc.  This file is licensed under the
 * terms of the GNU General Public License version 2.  This program is licensed
 * "as is" without any warranty of any kind, whether express or implied.
 */

#include <linux/types.h>
#include <asm/uaccess.h>
#include <asm/mpc52xx.h>
#include <asm/mpc52xx_psc.h>
#include <asm/serial.h>
#include <asm/io.h>
#include <asm/time.h>


#ifdef MPC52xx_PF_CONSOLE_PORT
#define MPC52xx_CONSOLE MPC52xx_PSCx_OFFSET(MPC52xx_PF_CONSOLE_PORT)
#define MPC52xx_PSC_CONFIG_SHIFT ((MPC52xx_PF_CONSOLE_PORT-1)<<2)
#else
#error "MPC52xx_PF_CONSOLE_PORT not defined"
#endif

static struct mpc52xx_psc __iomem *psc =
	(struct mpc52xx_psc __iomem *) MPC52xx_PA(MPC52xx_CONSOLE);

/* The decrementer counts at the system bus clock frequency
 * divided by four.  The most accurate time base is connected to the
 * rtc.  We read the decrementer change during one rtc tick
 * and multiply by 4 to get the system bus clock frequency. Since a
 * rtc tick is one seconds, and that's pretty long, we change the rtc
 * dividers temporarily to set them 64x faster ;)
 */
static int
mpc52xx_ipbfreq(void)
{
	struct mpc52xx_rtc __iomem *rtc =
		(struct mpc52xx_rtc __iomem *) MPC52xx_PA(MPC52xx_RTC_OFFSET);
	struct mpc52xx_cdm __iomem *cdm =
		(struct mpc52xx_cdm __iomem *) MPC52xx_PA(MPC52xx_CDM_OFFSET);
	int current_time, previous_time;
	int tbl_start, tbl_end;
	int xlbfreq, ipbfreq;

	out_be32(&rtc->dividers, 0x8f1f0000);	/* Set RTC 64x faster */
	previous_time = in_be32(&rtc->time);
	while ((current_time = in_be32(&rtc->time)) == previous_time) ;
	tbl_start = get_tbl();
	previous_time = current_time;
	while ((current_time = in_be32(&rtc->time)) == previous_time) ;
	tbl_end = get_tbl();
	out_be32(&rtc->dividers, 0xffff0000);   /* Restore RTC */

	xlbfreq = (tbl_end - tbl_start) << 8;
	ipbfreq = (in_8(&cdm->ipb_clk_sel) & 1) ? xlbfreq / 2 : xlbfreq;

	return ipbfreq;
}

unsigned long
serial_init(int ignored, void *ignored2)
{
	struct mpc52xx_gpio __iomem *gpio =
		(struct mpc52xx_gpio __iomem *) MPC52xx_PA(MPC52xx_GPIO_OFFSET);
	int divisor;
	int mode1;
	int mode2;
	u32 val32;

	static int been_here = 0;

	if (been_here)
		return 0;

	been_here = 1;

	val32 = in_be32(&gpio->port_config);
	val32 &= ~(0x7 << MPC52xx_PSC_CONFIG_SHIFT);
	val32 |= MPC52xx_GPIO_PSC_CONFIG_UART_WITHOUT_CD
				<< MPC52xx_PSC_CONFIG_SHIFT;
	out_be32(&gpio->port_config, val32);

	out_8(&psc->command, MPC52xx_PSC_RST_TX
			| MPC52xx_PSC_RX_DISABLE | MPC52xx_PSC_TX_ENABLE);
	out_8(&psc->command, MPC52xx_PSC_RST_RX);

	out_be32(&psc->sicr, 0x0);
	out_be16(&psc->mpc52xx_psc_clock_select, 0xdd00);
	out_be16(&psc->tfalarm, 0xf8);

	out_8(&psc->command, MPC52xx_PSC_SEL_MODE_REG_1
			| MPC52xx_PSC_RX_ENABLE
			| MPC52xx_PSC_TX_ENABLE);

	divisor = ((mpc52xx_ipbfreq()
			/ (CONFIG_SERIAL_MPC52xx_CONSOLE_BAUD * 16)) + 1) >> 1;

	mode1 = MPC52xx_PSC_MODE_8_BITS | MPC52xx_PSC_MODE_PARNONE
			| MPC52xx_PSC_MODE_ERR;
	mode2 = MPC52xx_PSC_MODE_ONE_STOP;

	out_8(&psc->ctur, divisor>>8);
	out_8(&psc->ctlr, divisor);
	out_8(&psc->command, MPC52xx_PSC_SEL_MODE_REG_1);
	out_8(&psc->mode, mode1);
	out_8(&psc->mode, mode2);

	return 0;	/* ignored */
}

void
serial_putc(void *ignored, const char c)
{
	serial_init(0, NULL);

	while (!(in_be16(&psc->mpc52xx_psc_status) & MPC52xx_PSC_SR_TXEMP)) ;
	out_8(&psc->mpc52xx_psc_buffer_8, c);
	while (!(in_be16(&psc->mpc52xx_psc_status) & MPC52xx_PSC_SR_TXEMP)) ;
}

char
serial_getc(void *ignored)
{
	while (!(in_be16(&psc->mpc52xx_psc_status) & MPC52xx_PSC_SR_RXRDY)) ;

	return in_8(&psc->mpc52xx_psc_buffer_8);
}

int
serial_tstc(void *ignored)
{
	return (in_be16(&psc->mpc52xx_psc_status) & MPC52xx_PSC_SR_RXRDY) != 0;
}
