/*
 * Jaguar-ATX Board Register Definitions
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
 */
#ifndef __JAGUAR_ATX_FPGA_H__
#define __JAGUAR_ATX_FPGA_H__

#define JAGUAR_ATX_REG_BOARDREV		0x0
#define JAGUAR_ATX_REG_FPGA_REV		0x1
#define JAGUAR_ATX_REG_FPGA_TYPE	0x2
#define JAGUAR_ATX_REG_RESET_STATUS	0x3
#define JAGUAR_ATX_REG_BOARD_STATUS	0x4
#define JAGUAR_ATX_REG_RESERVED1	0x5
#define JAGUAR_ATX_REG_SET		0x6
#define JAGUAR_ATX_REG_CLR		0x7
#define JAGUAR_ATX_REG_EEPROM_MODE	0x9
#define JAGUAR_ATX_REG_RESERVED2	0xa
#define JAGUAR_ATX_REG_RESERVED3	0xb
#define JAGUAR_ATX_REG_RESERVED4	0xc
#define JAGUAR_ATX_REG_PHY_INTSTAT	0xd
#define JAGUAR_ATX_REG_RESERVED5	0xe
#define JAGUAR_ATX_REG_RESERVED6	0xf

#define JAGUAR_ATX_CS0_ADDR		0xfc000000L

extern unsigned long ja_fpga_base;

#define JAGUAR_FPGA_WRITE(x,y) writeb(x, ja_fpga_base + JAGUAR_ATX_REG_##y)
#define JAGUAR_FPGA_READ(x) readb(ja_fpga_base + JAGUAR_ATX_REG_##x)

#endif
