/*
 *  arch/mips/pmc-sierra/yosemite/i2c-yosemite.h
 *
 *  Copyright (C) 2003 PMC-Sierra Inc.
 *  Author: Manish Lachwani (lachwani@pmc-sierra.com)
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

#ifndef __I2C_YOSEMITE_H
#define __I2C_YOSEMITE_H

/* Read and Write operations to the chip */

#define TITAN_I2C_BASE			0xbb000000	/* XXX Needs to change */

#define	TITAN_I2C_WRITE(offset, data)	\
					*(volatile unsigned long *)(TITAN_I2C_BASE + offset) = data

#define	TITAN_I2C_READ(offset) *(volatile unsigned long *)(TITAN_I2C_BASE + offset)


/* Local constansts*/
#define TITAN_I2C_MAX_FILTER            15
#define TITAN_I2C_MAX_CLK               1023
#define TITAN_I2C_MAX_ARBF              15
#define TITAN_I2C_MAX_NAK               15
#define TITAN_I2C_MAX_MASTERCODE        7
#define TITAN_I2C_MAX_WORDS_PER_RW      4
#define TITAN_I2C_MAX_POLL		100

/* Registers used for I2C work */
#define TITAN_I2C_SCMB_CONTROL		0x0180	/* SCMB Control */
#define TITAN_I2C_SCMB_CLOCK_A		0x0184	/* SCMB Clock A */
#define TITAN_I2C_SCMB_CLOCK_B		0x0188	/* SCMB Clock B */
#define	TITAN_I2C_CONFIG		0x01A0	/* I2C Config */
#define TITAN_I2C_COMMAND		0x01A4	/* I2C Command */
#define	TITAN_I2C_SLAVE_ADDRESS		0x01A8	/* I2C Slave Address */
#define TITAN_I2C_DATA			0x01AC	/* I2C Data [15:0] */
#define TITAN_I2C_INTERRUPTS		0x01BC	/* I2C Interrupts */

/* Error */
#define	TITAN_I2C_ERR_ARB_LOST		(-9220)
#define	TITAN_I2C_ERR_NO_RESP		(-9221)
#define	TITAN_I2C_ERR_DATA_COLLISION	(-9222)
#define	TITAN_I2C_ERR_TIMEOUT		(-9223)
#define	TITAN_I2C_ERR_OK		0

/* I2C Command Type */
typedef enum {
	TITAN_I2C_CMD_WRITE = 0,
	TITAN_I2C_CMD_READ = 1,
	TITAN_I2C_CMD_READ_WRITE = 2
} titan_i2c_cmd_type;

/* I2C structures */
typedef struct {
	int filtera;		/* Register 0x0184, bits 15 - 12 */
	int clka;		/* Register 0x0184, bits 9 - 0 */
	int filterb;		/* Register 0x0188, bits 15 - 12 */
	int clkb;		/* Register 0x0188, bits 9 - 0 */
} titan_i2c_config;

/* I2C command type */
typedef struct {
	titan_i2c_cmd_type type;	/* Type of command */
	int num_arb;		/* Register 0x01a0, bits 15 - 12 */
	int num_nak;		/* Register 0x01a0, bits 11 - 8 */
	int addr_size;		/* Register 0x01a0, bit 7 */
	int mst_code;		/* Register 0x01a0, bits 6 - 4 */
	int arb_en;		/* Register 0x01a0, bit 1 */
	int speed;		/* Register 0x01a0, bit 0 */
	int slave_addr;		/* Register 0x01a8 */
	int write_size;		/* Register 0x01a4, bits 10 - 8 */
	unsigned int *data;	/* Register 0x01ac */
} titan_i2c_command;

#endif				/* __I2C_YOSEMITE_H */
