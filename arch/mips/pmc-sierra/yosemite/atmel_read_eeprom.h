/*
 *  arch/mips/pmc-sierra/yosemite/atmel_read_eeprom.c
 *
 *  Copyright (C) 2003 PMC-Sierra Inc.
 *  Author: Manish Lachwani (lachwani@pmc-sierra.com)
 *  Copyright (C) 2005 Ralf Baechle (ralf@linux-mips.org)
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

/*
 * Header file for atmel_read_eeprom.c
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <asm/pci.h>
#include <asm/io.h>
#include <linux/init.h>
#include <asm/termios.h>
#include <asm/ioctls.h>
#include <linux/ioctl.h>
#include <linux/fcntl.h>

#define	DEFAULT_PORT 	"/dev/ttyS0"	/* Port to open */
#define	TXX		0 		/* Dummy loop for spinning */

#define	BLOCK_SEL	0x00
#define	SLAVE_ADDR	0xa0
#define	READ_BIT	0x01
#define	WRITE_BIT	0x00
#define	R_HEADER	SLAVE_ADDR + BLOCK_SEL + READ_BIT
#define	W_HEADER	SLAVE_ADDR + BLOCK_SEL + WRITE_BIT

/*
 * Clock, Voltages and Data
 */
#define	vcc_off		(ioctl(fd, TIOCSBRK, 0))
#define	vcc_on		(ioctl(fd, TIOCCBRK, 0))
#define	sda_hi		(ioctl(fd, TIOCMBIS, &dtr))
#define	sda_lo		(ioctl(fd, TIOCMBIC, &dtr))
#define	scl_lo		(ioctl(fd, TIOCMBIC, &rts))
#define	scl_hi		(ioctl(fd, TIOCMBIS, &rts))

const char rts = TIOCM_RTS;
const char dtr = TIOCM_DTR;
int fd;

