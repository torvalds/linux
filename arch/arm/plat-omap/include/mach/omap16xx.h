/* arch/arm/plat-omap/include/mach/omap16xx.h
 *
 * Hardware definitions for TI OMAP1610/5912/1710 processors.
 *
 * Cleanup for Linux-2.6 by Dirk Behme <dirk.behme@de.bosch.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ASM_ARCH_OMAP16XX_H
#define __ASM_ARCH_OMAP16XX_H

/*
 * ----------------------------------------------------------------------------
 * Base addresses
 * ----------------------------------------------------------------------------
 */

/* Syntax: XX_BASE = Virtual base address, XX_START = Physical base address */

#define OMAP16XX_DSP_BASE	0xE0000000
#define OMAP16XX_DSP_SIZE	0x28000
#define OMAP16XX_DSP_START	0xE0000000

#define OMAP16XX_DSPREG_BASE	0xE1000000
#define OMAP16XX_DSPREG_SIZE	SZ_128K
#define OMAP16XX_DSPREG_START	0xE1000000

/*
 * ---------------------------------------------------------------------------
 * Interrupts
 * ---------------------------------------------------------------------------
 */
#define OMAP_IH2_0_BASE		(0xfffe0000)
#define OMAP_IH2_1_BASE		(0xfffe0100)
#define OMAP_IH2_2_BASE		(0xfffe0200)
#define OMAP_IH2_3_BASE		(0xfffe0300)

#define OMAP_IH2_0_ITR		(OMAP_IH2_0_BASE + 0x00)
#define OMAP_IH2_0_MIR		(OMAP_IH2_0_BASE + 0x04)
#define OMAP_IH2_0_SIR_IRQ	(OMAP_IH2_0_BASE + 0x10)
#define OMAP_IH2_0_SIR_FIQ	(OMAP_IH2_0_BASE + 0x14)
#define OMAP_IH2_0_CONTROL	(OMAP_IH2_0_BASE + 0x18)
#define OMAP_IH2_0_ILR0		(OMAP_IH2_0_BASE + 0x1c)
#define OMAP_IH2_0_ISR		(OMAP_IH2_0_BASE + 0x9c)

#define OMAP_IH2_1_ITR		(OMAP_IH2_1_BASE + 0x00)
#define OMAP_IH2_1_MIR		(OMAP_IH2_1_BASE + 0x04)
#define OMAP_IH2_1_SIR_IRQ	(OMAP_IH2_1_BASE + 0x10)
#define OMAP_IH2_1_SIR_FIQ	(OMAP_IH2_1_BASE + 0x14)
#define OMAP_IH2_1_CONTROL	(OMAP_IH2_1_BASE + 0x18)
#define OMAP_IH2_1_ILR1		(OMAP_IH2_1_BASE + 0x1c)
#define OMAP_IH2_1_ISR		(OMAP_IH2_1_BASE + 0x9c)

#define OMAP_IH2_2_ITR		(OMAP_IH2_2_BASE + 0x00)
#define OMAP_IH2_2_MIR		(OMAP_IH2_2_BASE + 0x04)
#define OMAP_IH2_2_SIR_IRQ	(OMAP_IH2_2_BASE + 0x10)
#define OMAP_IH2_2_SIR_FIQ	(OMAP_IH2_2_BASE + 0x14)
#define OMAP_IH2_2_CONTROL	(OMAP_IH2_2_BASE + 0x18)
#define OMAP_IH2_2_ILR2		(OMAP_IH2_2_BASE + 0x1c)
#define OMAP_IH2_2_ISR		(OMAP_IH2_2_BASE + 0x9c)

#define OMAP_IH2_3_ITR		(OMAP_IH2_3_BASE + 0x00)
#define OMAP_IH2_3_MIR		(OMAP_IH2_3_BASE + 0x04)
#define OMAP_IH2_3_SIR_IRQ	(OMAP_IH2_3_BASE + 0x10)
#define OMAP_IH2_3_SIR_FIQ	(OMAP_IH2_3_BASE + 0x14)
#define OMAP_IH2_3_CONTROL	(OMAP_IH2_3_BASE + 0x18)
#define OMAP_IH2_3_ILR3		(OMAP_IH2_3_BASE + 0x1c)
#define OMAP_IH2_3_ISR		(OMAP_IH2_3_BASE + 0x9c)

/*
 * ----------------------------------------------------------------------------
 * Clocks
 * ----------------------------------------------------------------------------
 */
#define OMAP16XX_ARM_IDLECT3	(CLKGEN_REG_BASE + 0x24)

/*
 * ----------------------------------------------------------------------------
 * Pin configuration registers
 * ----------------------------------------------------------------------------
 */
#define OMAP16XX_CONF_VOLTAGE_VDDSHV6	(1 << 8)
#define OMAP16XX_CONF_VOLTAGE_VDDSHV7	(1 << 9)
#define OMAP16XX_CONF_VOLTAGE_VDDSHV8	(1 << 10)
#define OMAP16XX_CONF_VOLTAGE_VDDSHV9	(1 << 11)
#define OMAP16XX_SUBLVDS_CONF_VALID	(1 << 13)

/*
 * ----------------------------------------------------------------------------
 * System control registers
 * ----------------------------------------------------------------------------
 */
#define OMAP1610_RESET_CONTROL  0xfffe1140

/*
 * ---------------------------------------------------------------------------
 * TIPB bus interface
 * ---------------------------------------------------------------------------
 */
#define TIPB_SWITCH_BASE		 (0xfffbc800)
#define OMAP16XX_MMCSD2_SSW_MPU_CONF	(TIPB_SWITCH_BASE + 0x160)

/* UART3 Registers Maping through MPU bus */
#define UART3_RHR               (OMAP_UART3_BASE + 0)
#define UART3_THR               (OMAP_UART3_BASE + 0)
#define UART3_DLL               (OMAP_UART3_BASE + 0)
#define UART3_IER               (OMAP_UART3_BASE + 4)
#define UART3_DLH               (OMAP_UART3_BASE + 4)
#define UART3_IIR               (OMAP_UART3_BASE + 8)
#define UART3_FCR               (OMAP_UART3_BASE + 8)
#define UART3_EFR               (OMAP_UART3_BASE + 8)
#define UART3_LCR               (OMAP_UART3_BASE + 0x0C)
#define UART3_MCR               (OMAP_UART3_BASE + 0x10)
#define UART3_XON1_ADDR1        (OMAP_UART3_BASE + 0x10)
#define UART3_XON2_ADDR2        (OMAP_UART3_BASE + 0x14)
#define UART3_LSR               (OMAP_UART3_BASE + 0x14)
#define UART3_TCR               (OMAP_UART3_BASE + 0x18)
#define UART3_MSR               (OMAP_UART3_BASE + 0x18)
#define UART3_XOFF1             (OMAP_UART3_BASE + 0x18)
#define UART3_XOFF2             (OMAP_UART3_BASE + 0x1C)
#define UART3_SPR               (OMAP_UART3_BASE + 0x1C)
#define UART3_TLR               (OMAP_UART3_BASE + 0x1C)
#define UART3_MDR1              (OMAP_UART3_BASE + 0x20)
#define UART3_MDR2              (OMAP_UART3_BASE + 0x24)
#define UART3_SFLSR             (OMAP_UART3_BASE + 0x28)
#define UART3_TXFLL             (OMAP_UART3_BASE + 0x28)
#define UART3_RESUME            (OMAP_UART3_BASE + 0x2C)
#define UART3_TXFLH             (OMAP_UART3_BASE + 0x2C)
#define UART3_SFREGL            (OMAP_UART3_BASE + 0x30)
#define UART3_RXFLL             (OMAP_UART3_BASE + 0x30)
#define UART3_SFREGH            (OMAP_UART3_BASE + 0x34)
#define UART3_RXFLH             (OMAP_UART3_BASE + 0x34)
#define UART3_BLR               (OMAP_UART3_BASE + 0x38)
#define UART3_ACREG             (OMAP_UART3_BASE + 0x3C)
#define UART3_DIV16             (OMAP_UART3_BASE + 0x3C)
#define UART3_SCR               (OMAP_UART3_BASE + 0x40)
#define UART3_SSR               (OMAP_UART3_BASE + 0x44)
#define UART3_EBLR              (OMAP_UART3_BASE + 0x48)
#define UART3_OSC_12M_SEL       (OMAP_UART3_BASE + 0x4C)
#define UART3_MVR               (OMAP_UART3_BASE + 0x50)

/*
 * ---------------------------------------------------------------------------
 * Watchdog timer
 * ---------------------------------------------------------------------------
 */

/* 32-bit Watchdog timer in OMAP 16XX */
#define OMAP_16XX_WATCHDOG_BASE        (0xfffeb000)
#define OMAP_16XX_WIDR         (OMAP_16XX_WATCHDOG_BASE + 0x00)
#define OMAP_16XX_WD_SYSCONFIG (OMAP_16XX_WATCHDOG_BASE + 0x10)
#define OMAP_16XX_WD_SYSSTATUS (OMAP_16XX_WATCHDOG_BASE + 0x14)
#define OMAP_16XX_WCLR         (OMAP_16XX_WATCHDOG_BASE + 0x24)
#define OMAP_16XX_WCRR         (OMAP_16XX_WATCHDOG_BASE + 0x28)
#define OMAP_16XX_WLDR         (OMAP_16XX_WATCHDOG_BASE + 0x2c)
#define OMAP_16XX_WTGR         (OMAP_16XX_WATCHDOG_BASE + 0x30)
#define OMAP_16XX_WWPS         (OMAP_16XX_WATCHDOG_BASE + 0x34)
#define OMAP_16XX_WSPR         (OMAP_16XX_WATCHDOG_BASE + 0x48)

#define WCLR_PRE_SHIFT         5
#define WCLR_PTV_SHIFT         2

#define WWPS_W_PEND_WSPR       (1 << 4)
#define WWPS_W_PEND_WTGR       (1 << 3)
#define WWPS_W_PEND_WLDR       (1 << 2)
#define WWPS_W_PEND_WCRR       (1 << 1)
#define WWPS_W_PEND_WCLR       (1 << 0)

#define WSPR_ENABLE_0          (0x0000bbbb)
#define WSPR_ENABLE_1          (0x00004444)
#define WSPR_DISABLE_0         (0x0000aaaa)
#define WSPR_DISABLE_1         (0x00005555)

/* Mailbox */
#define OMAP16XX_MAILBOX_BASE	(0xfffcf000)

#endif /*  __ASM_ARCH_OMAP16XX_H */

