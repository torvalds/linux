/*
 * Ocelot-C Board Register Definitions
 *
 * (C) 2002 Momentum Computer Inc.
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
 *  Louis Hamilton, Red Hat, Inc.
 *    hamilton@redhat.com  [MIPS64 modifications]
 */

#ifndef __OCELOT_C_FPGA_H__
#define __OCELOT_C_FPGA_H__


#ifdef CONFIG_64BIT
#define OCELOT_C_CS0_ADDR       (0xfffffffffc000000)
#else
#define OCELOT_C_CS0_ADDR               (0xfc000000)
#endif

#define OCELOT_C_REG_BOARDREV		0x0
#define OCELOT_C_REG_FPGA_REV		0x1
#define OCELOT_C_REG_FPGA_TYPE		0x2
#define OCELOT_C_REG_RESET_STATUS	0x3
#define OCELOT_C_REG_BOARD_STATUS	0x4
#define OCELOT_C_REG_CPCI_ID		0x5
#define OCELOT_C_REG_SET		0x6
#define OCELOT_C_REG_CLR		0x7
#define OCELOT_C_REG_EEPROM_MODE	0x9
#define OCELOT_C_REG_INTMASK		0xa
#define OCELOT_C_REG_INTSTAT		0xb
#define OCELOT_C_REG_UART_INTMASK	0xc
#define OCELOT_C_REG_UART_INTSTAT	0xd
#define OCELOT_C_REG_INTSET		0xe
#define OCELOT_C_REG_INTCLR		0xf

#define __FPGA_REG_TO_ADDR(reg)						\
	((void *) OCELOT_C_CS0_ADDR + OCELOT_C_REG_##reg)
#define OCELOT_FPGA_WRITE(x, reg) writeb(x, __FPGA_REG_TO_ADDR(reg))
#define OCELOT_FPGA_READ(reg) readb(__FPGA_REG_TO_ADDR(reg))

#endif
