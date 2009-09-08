/*
 *  DaVinci Power & Sleep Controller (PSC) defines
 *
 *  Copyright (C) 2006 Texas Instruments.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#ifndef __ASM_ARCH_PSC_H
#define __ASM_ARCH_PSC_H

#define	DAVINCI_PWR_SLEEP_CNTRL_BASE	0x01C41000

/* Power and Sleep Controller (PSC) Domains */
#define DAVINCI_GPSC_ARMDOMAIN      0
#define DAVINCI_GPSC_DSPDOMAIN      1

#define DAVINCI_LPSC_VPSSMSTR       0
#define DAVINCI_LPSC_VPSSSLV        1
#define DAVINCI_LPSC_TPCC           2
#define DAVINCI_LPSC_TPTC0          3
#define DAVINCI_LPSC_TPTC1          4
#define DAVINCI_LPSC_EMAC           5
#define DAVINCI_LPSC_EMAC_WRAPPER   6
#define DAVINCI_LPSC_USB            9
#define DAVINCI_LPSC_ATA            10
#define DAVINCI_LPSC_VLYNQ          11
#define DAVINCI_LPSC_UHPI           12
#define DAVINCI_LPSC_DDR_EMIF       13
#define DAVINCI_LPSC_AEMIF          14
#define DAVINCI_LPSC_MMC_SD         15
#define DAVINCI_LPSC_McBSP          17
#define DAVINCI_LPSC_I2C            18
#define DAVINCI_LPSC_UART0          19
#define DAVINCI_LPSC_UART1          20
#define DAVINCI_LPSC_UART2          21
#define DAVINCI_LPSC_SPI            22
#define DAVINCI_LPSC_PWM0           23
#define DAVINCI_LPSC_PWM1           24
#define DAVINCI_LPSC_PWM2           25
#define DAVINCI_LPSC_GPIO           26
#define DAVINCI_LPSC_TIMER0         27
#define DAVINCI_LPSC_TIMER1         28
#define DAVINCI_LPSC_TIMER2         29
#define DAVINCI_LPSC_SYSTEM_SUBSYS  30
#define DAVINCI_LPSC_ARM            31
#define DAVINCI_LPSC_SCR2           32
#define DAVINCI_LPSC_SCR3           33
#define DAVINCI_LPSC_SCR4           34
#define DAVINCI_LPSC_CROSSBAR       35
#define DAVINCI_LPSC_CFG27          36
#define DAVINCI_LPSC_CFG3           37
#define DAVINCI_LPSC_CFG5           38
#define DAVINCI_LPSC_GEM            39
#define DAVINCI_LPSC_IMCOP          40

#define DM355_LPSC_TIMER3		5
#define DM355_LPSC_SPI1			6
#define DM355_LPSC_MMC_SD1		7
#define DM355_LPSC_McBSP1		8
#define DM355_LPSC_PWM3			10
#define DM355_LPSC_SPI2			11
#define DM355_LPSC_RTO			12
#define DM355_LPSC_VPSS_DAC		41

/*
 * LPSC Assignments
 */
#define DM646X_LPSC_ARM            0
#define DM646X_LPSC_C64X_CPU       1
#define DM646X_LPSC_HDVICP0        2
#define DM646X_LPSC_HDVICP1        3
#define DM646X_LPSC_TPCC           4
#define DM646X_LPSC_TPTC0          5
#define DM646X_LPSC_TPTC1          6
#define DM646X_LPSC_TPTC2          7
#define DM646X_LPSC_TPTC3          8
#define DM646X_LPSC_PCI            13
#define DM646X_LPSC_EMAC           14
#define DM646X_LPSC_VDCE           15
#define DM646X_LPSC_VPSSMSTR       16
#define DM646X_LPSC_VPSSSLV        17
#define DM646X_LPSC_TSIF0          18
#define DM646X_LPSC_TSIF1          19
#define DM646X_LPSC_DDR_EMIF       20
#define DM646X_LPSC_AEMIF          21
#define DM646X_LPSC_McASP0         22
#define DM646X_LPSC_McASP1         23
#define DM646X_LPSC_CRGEN0         24
#define DM646X_LPSC_CRGEN1         25
#define DM646X_LPSC_UART0          26
#define DM646X_LPSC_UART1          27
#define DM646X_LPSC_UART2          28
#define DM646X_LPSC_PWM0           29
#define DM646X_LPSC_PWM1           30
#define DM646X_LPSC_I2C            31
#define DM646X_LPSC_SPI            32
#define DM646X_LPSC_GPIO           33
#define DM646X_LPSC_TIMER0         34
#define DM646X_LPSC_TIMER1         35
#define DM646X_LPSC_ARM_INTC       45

extern int davinci_psc_is_clk_active(unsigned int ctlr, unsigned int id);
extern void davinci_psc_config(unsigned int domain, unsigned int ctlr,
		unsigned int id, char enable);

#endif /* __ASM_ARCH_PSC_H */
