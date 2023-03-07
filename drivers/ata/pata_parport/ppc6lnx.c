/*
	ppc6lnx.c (c) 2001 Micro Solutions Inc.
		Released under the terms of the GNU General Public license

	ppc6lnx.c  is a par of the protocol driver for the Micro Solutions
		"BACKPACK" parallel port IDE adapter
		(Works on Series 6 drives)

*/

//***************************************************************************

// PPC 6 Code in C sanitized for LINUX
// Original x86 ASM by Ron, Converted to C by Clive

//***************************************************************************

//  60772 Commands

#define ACCESS_REG				0x00
#define ACCESS_PORT				0x40

#define ACCESS_READ				0x00
#define ACCESS_WRITE			0x20

//  60772 Command Prefix

#define CMD_PREFIX_SET		0xe0		// Special command that modifies the next command's operation
#define CMD_PREFIX_RESET	0xc0		// Resets current cmd modifier reg bits
 #define PREFIX_IO16			0x01		// perform 16-bit wide I/O
 #define PREFIX_FASTWR		0x04		// enable PPC mode fast-write
 #define PREFIX_BLK				0x08		// enable block transfer mode

// 60772 Registers

#define REG_STATUS				0x00		// status register
 #define STATUS_IRQA			0x01		// Peripheral IRQA line
 #define STATUS_EEPROM_DO	0x40		// Serial EEPROM data bit
#define REG_VERSION				0x01		// PPC version register (read)
#define REG_HWCFG					0x02		// Hardware Config register
#define REG_RAMSIZE				0x03		// Size of RAM Buffer
 #define RAMSIZE_128K			0x02
#define REG_EEPROM				0x06		// EEPROM control register
 #define EEPROM_SK				0x01		// eeprom SK bit
 #define EEPROM_DI				0x02		// eeprom DI bit
 #define EEPROM_CS				0x04		// eeprom CS bit
 #define EEPROM_EN				0x08		// eeprom output enable
#define REG_BLKSIZE				0x08		// Block transfer len (24 bit)

//***************************************************************************

#define PPC_FLAGS	(((u8 *)&pi->private)[1])

//***************************************************************************

// ppc_flags

#define fifo_wait					0x10

//***************************************************************************

// DONT CHANGE THESE LEST YOU BREAK EVERYTHING - BIT FIELD DEPENDENCIES

#define PPCMODE_UNI_SW		0
#define PPCMODE_UNI_FW		1
#define PPCMODE_BI_SW			2
#define PPCMODE_BI_FW			3
#define PPCMODE_EPP_BYTE	4
#define PPCMODE_EPP_WORD	5
#define PPCMODE_EPP_DWORD	6

//***************************************************************************

static int ppc6_select(struct pi_adapter *pi);
static void ppc6_deselect(struct pi_adapter *pi);
static void ppc6_send_cmd(struct pi_adapter *pi, u8 cmd);
static void ppc6_wr_data_byte(struct pi_adapter *pi, u8 data);
static u8 ppc6_rd_data_byte(struct pi_adapter *pi);
static u8 ppc6_rd_port(struct pi_adapter *pi, u8 port);
static void ppc6_wr_port(struct pi_adapter *pi, u8 port, u8 data);
static void ppc6_rd_data_blk(struct pi_adapter *pi, u8 *data, long count);
static void ppc6_wait_for_fifo(struct pi_adapter *pi);
static void ppc6_wr_data_blk(struct pi_adapter *pi, u8 *data, long count);
static void ppc6_rd_port16_blk(struct pi_adapter *pi, u8 port, u8 *data, long length);
static void ppc6_wr_port16_blk(struct pi_adapter *pi, u8 port, u8 *data, long length);
static void ppc6_wr_extout(struct pi_adapter *pi, u8 regdata);
static int ppc6_open(struct pi_adapter *pi);
static void ppc6_close(struct pi_adapter *pi);

//***************************************************************************

int mode_map[] = { PPCMODE_UNI_FW, PPCMODE_BI_FW, PPCMODE_EPP_BYTE,
		   PPCMODE_EPP_WORD, PPCMODE_EPP_DWORD };

static int ppc6_select(struct pi_adapter *pi)
{
	u8 i, j, k;

	pi->saved_r0 = parport_read_data(pi->pardev->port);

	pi->saved_r2 = parport_read_control(pi->pardev->port) & 0x5F; // readback ctrl

	parport_frob_control(pi->pardev->port, PARPORT_CONTROL_SELECT, PARPORT_CONTROL_SELECT);

	if (pi->saved_r0 == 'b')
		parport_write_data(pi->pardev->port, 'x');

	parport_write_data(pi->pardev->port, 'b');
	parport_write_data(pi->pardev->port, 'p');
	parport_write_data(pi->pardev->port, pi->unit);
	parport_write_data(pi->pardev->port, ~pi->unit);

	parport_frob_control(pi->pardev->port, PARPORT_CONTROL_SELECT, 0);

	parport_write_control(pi->pardev->port, PARPORT_CONTROL_INIT);

	i = mode_map[pi->mode] & 0x0C;

	if (i == 0)
		i = (mode_map[pi->mode] & 2) | 1;

	parport_write_data(pi->pardev->port, i);

	parport_frob_control(pi->pardev->port, PARPORT_CONTROL_SELECT, PARPORT_CONTROL_SELECT);

	// DELAY

	parport_frob_control(pi->pardev->port, PARPORT_CONTROL_AUTOFD, PARPORT_CONTROL_AUTOFD);

	j = ((i & 0x08) << 4) | ((i & 0x07) << 3);

	k = parport_read_status(pi->pardev->port) & 0xB8;

	if (j == k)
	{
		parport_frob_control(pi->pardev->port, PARPORT_CONTROL_AUTOFD, 0);

		k = (parport_read_status(pi->pardev->port) & 0xB8) ^ 0xB8;

		if (j == k)
		{
			if (i & 4)	// EPP
				parport_frob_control(pi->pardev->port,
					PARPORT_CONTROL_SELECT | PARPORT_CONTROL_INIT, 0);
			else				// PPC/ECP
				parport_frob_control(pi->pardev->port,
					PARPORT_CONTROL_SELECT, 0);

			return(1);
		}
	}

	parport_write_control(pi->pardev->port, pi->saved_r2);

	parport_write_data(pi->pardev->port, pi->saved_r0);

	return(0); // FAIL
}

//***************************************************************************

static void ppc6_deselect(struct pi_adapter *pi)
{
	if (mode_map[pi->mode] & 4)	// EPP
		parport_frob_control(pi->pardev->port,
			PARPORT_CONTROL_INIT, PARPORT_CONTROL_INIT);
	else								// PPC/ECP
		parport_frob_control(pi->pardev->port,
			PARPORT_CONTROL_SELECT, PARPORT_CONTROL_SELECT);

	parport_write_data(pi->pardev->port, pi->saved_r0);

	parport_write_control(pi->pardev->port, (pi->saved_r2 | PARPORT_CONTROL_SELECT));

	parport_write_control(pi->pardev->port, pi->saved_r2);
}

//***************************************************************************

static void ppc6_send_cmd(struct pi_adapter *pi, u8 cmd)
{
	switch (mode_map[pi->mode])
	{
		case PPCMODE_UNI_SW :
		case PPCMODE_UNI_FW :
		case PPCMODE_BI_SW :
		case PPCMODE_BI_FW :
		{
			parport_write_data(pi->pardev->port, cmd);
			parport_frob_control(pi->pardev->port, 0, PARPORT_CONTROL_AUTOFD);

			break;
		}

		case PPCMODE_EPP_BYTE :
		case PPCMODE_EPP_WORD :
		case PPCMODE_EPP_DWORD :
		{
			pi->pardev->port->ops->epp_write_addr(pi->pardev->port, &cmd, 1, 0);

			break;
		}
	}
}

//***************************************************************************

static void ppc6_wr_data_byte(struct pi_adapter *pi, u8 data)
{
	switch (mode_map[pi->mode])
	{
		case PPCMODE_UNI_SW :
		case PPCMODE_UNI_FW :
		case PPCMODE_BI_SW :
		case PPCMODE_BI_FW :
		{
			parport_write_data(pi->pardev->port, data);
			parport_frob_control(pi->pardev->port, 0, PARPORT_CONTROL_INIT);

			break;
		}

		case PPCMODE_EPP_BYTE :
		case PPCMODE_EPP_WORD :
		case PPCMODE_EPP_DWORD :
		{
			pi->pardev->port->ops->epp_write_data(pi->pardev->port, &data, 1, 0);

			break;
		}
	}
}

//***************************************************************************

static u8 ppc6_rd_data_byte(struct pi_adapter *pi)
{
	u8 data = 0;

	switch (mode_map[pi->mode])
	{
		case PPCMODE_UNI_SW :
		case PPCMODE_UNI_FW :
		{
			parport_frob_control(pi->pardev->port,
				PARPORT_CONTROL_STROBE, PARPORT_CONTROL_INIT);

			// DELAY

			data = parport_read_status(pi->pardev->port);

			data = ((data & 0x80) >> 1) | ((data & 0x38) >> 3);

			parport_frob_control(pi->pardev->port,
				PARPORT_CONTROL_STROBE, PARPORT_CONTROL_STROBE);

			// DELAY

			data |= parport_read_status(pi->pardev->port) & 0xB8;

			break;
		}

		case PPCMODE_BI_SW :
		case PPCMODE_BI_FW :
		{
			parport_data_reverse(pi->pardev->port);

			parport_frob_control(pi->pardev->port, PARPORT_CONTROL_STROBE,
				PARPORT_CONTROL_STROBE | PARPORT_CONTROL_INIT);

			data = parport_read_data(pi->pardev->port);

			parport_frob_control(pi->pardev->port, PARPORT_CONTROL_STROBE, 0);

			parport_data_forward(pi->pardev->port);

			break;
		}

		case PPCMODE_EPP_BYTE :
		case PPCMODE_EPP_WORD :
		case PPCMODE_EPP_DWORD :
		{
			pi->pardev->port->ops->epp_read_data(pi->pardev->port, &data, 1, 0);

			break;
		}
	}

	return(data);
}

//***************************************************************************

static u8 ppc6_rd_port(struct pi_adapter *pi, u8 port)
{
	ppc6_send_cmd(pi, port | ACCESS_PORT | ACCESS_READ);

	return ppc6_rd_data_byte(pi);
}

//***************************************************************************

static void ppc6_wr_port(struct pi_adapter *pi, u8 port, u8 data)
{
	ppc6_send_cmd(pi, port | ACCESS_PORT | ACCESS_WRITE);

	ppc6_wr_data_byte(pi, data);
}

//***************************************************************************

static void ppc6_rd_data_blk(struct pi_adapter *pi, u8 *data, long count)
{
	switch (mode_map[pi->mode])
	{
		case PPCMODE_UNI_SW :
		case PPCMODE_UNI_FW :
		{
			while(count)
			{
				u8 d;

				parport_frob_control(pi->pardev->port,
					PARPORT_CONTROL_STROBE, PARPORT_CONTROL_INIT);

				// DELAY

				d = parport_read_status(pi->pardev->port);

				d = ((d & 0x80) >> 1) | ((d & 0x38) >> 3);

				parport_frob_control(pi->pardev->port,
					PARPORT_CONTROL_STROBE, PARPORT_CONTROL_STROBE);

				// DELAY

				d |= parport_read_status(pi->pardev->port) & 0xB8;

				*data++ = d;
				count--;
			}

			break;
		}

		case PPCMODE_BI_SW :
		case PPCMODE_BI_FW :
		{
			parport_data_reverse(pi->pardev->port);

			while(count)
			{
				parport_frob_control(pi->pardev->port, PARPORT_CONTROL_STROBE,
					PARPORT_CONTROL_STROBE | PARPORT_CONTROL_INIT);

				*data++ = parport_read_data(pi->pardev->port);
				count--;
			}

			parport_frob_control(pi->pardev->port, PARPORT_CONTROL_STROBE, 0);

			parport_data_forward(pi->pardev->port);

			break;
		}

		case PPCMODE_EPP_BYTE :
		{
			// DELAY

			pi->pardev->port->ops->epp_read_data(pi->pardev->port,
					data, count, PARPORT_EPP_FAST_8);

			break;
		}

		case PPCMODE_EPP_WORD :
		{
			// DELAY

			pi->pardev->port->ops->epp_read_data(pi->pardev->port,
					data, count, PARPORT_EPP_FAST_16);

			break;
		}

		case PPCMODE_EPP_DWORD :
		{
			// DELAY

			pi->pardev->port->ops->epp_read_data(pi->pardev->port,
					data, count, PARPORT_EPP_FAST_32);

			break;
		}
	}

}

//***************************************************************************

static void ppc6_wait_for_fifo(struct pi_adapter *pi)
{
	int i;

	if (PPC_FLAGS & fifo_wait)
	{
		for(i=0; i<20; i++)
			parport_read_status(pi->pardev->port);
	}
}

//***************************************************************************

static void ppc6_wr_data_blk(struct pi_adapter *pi, u8 *data, long count)
{
	switch (mode_map[pi->mode])
	{
		case PPCMODE_UNI_SW :
		case PPCMODE_BI_SW :
		{
			while(count--)
			{
				parport_write_data(pi->pardev->port, *data++);

				parport_frob_control(pi->pardev->port, 0, PARPORT_CONTROL_INIT);
			}

			break;
		}

		case PPCMODE_UNI_FW :
		case PPCMODE_BI_FW :
		{
			u8 this, last;

			ppc6_send_cmd(pi, CMD_PREFIX_SET | PREFIX_FASTWR);

			parport_frob_control(pi->pardev->port,
				PARPORT_CONTROL_STROBE, PARPORT_CONTROL_STROBE);

			last = *data;

			parport_write_data(pi->pardev->port, last);

			while(count)
			{
				this = *data++;
				count--;

				if (this == last)
				{
					parport_frob_control(pi->pardev->port,
						0, PARPORT_CONTROL_INIT);
				}
				else
				{
					parport_write_data(pi->pardev->port, this);

					last = this;
				}
			}

			parport_frob_control(pi->pardev->port, PARPORT_CONTROL_STROBE, 0);

			ppc6_send_cmd(pi, CMD_PREFIX_RESET | PREFIX_FASTWR);

			break;
		}

		case PPCMODE_EPP_BYTE :
		{
			pi->pardev->port->ops->epp_write_data(pi->pardev->port,
					data, count, PARPORT_EPP_FAST_8);

			ppc6_wait_for_fifo(pi);

			break;
		}

		case PPCMODE_EPP_WORD :
		{
			pi->pardev->port->ops->epp_write_data(pi->pardev->port,
					data, count, PARPORT_EPP_FAST_16);

			ppc6_wait_for_fifo(pi);

			break;
		}

		case PPCMODE_EPP_DWORD :
		{
			pi->pardev->port->ops->epp_write_data(pi->pardev->port,
					data, count, PARPORT_EPP_FAST_32);

			ppc6_wait_for_fifo(pi);

			break;
		}
	}
}

//***************************************************************************

static void ppc6_rd_port16_blk(struct pi_adapter *pi, u8 port, u8 *data, long length)
{
	length = length << 1;

	ppc6_send_cmd(pi, REG_BLKSIZE | ACCESS_REG | ACCESS_WRITE);
	ppc6_wr_data_byte(pi, (u8)length);
	ppc6_wr_data_byte(pi, (u8)(length >> 8));
	ppc6_wr_data_byte(pi, 0);

	ppc6_send_cmd(pi, CMD_PREFIX_SET | PREFIX_IO16 | PREFIX_BLK);

	ppc6_send_cmd(pi, port | ACCESS_PORT | ACCESS_READ);

	ppc6_rd_data_blk(pi, data, length);

	ppc6_send_cmd(pi, CMD_PREFIX_RESET | PREFIX_IO16 | PREFIX_BLK);
}

//***************************************************************************

static void ppc6_wr_port16_blk(struct pi_adapter *pi, u8 port, u8 *data, long length)
{
	length = length << 1;

	ppc6_send_cmd(pi, REG_BLKSIZE | ACCESS_REG | ACCESS_WRITE);
	ppc6_wr_data_byte(pi, (u8)length);
	ppc6_wr_data_byte(pi, (u8)(length >> 8));
	ppc6_wr_data_byte(pi, 0);

	ppc6_send_cmd(pi, CMD_PREFIX_SET | PREFIX_IO16 | PREFIX_BLK);

	ppc6_send_cmd(pi, port | ACCESS_PORT | ACCESS_WRITE);

	ppc6_wr_data_blk(pi, data, length);

	ppc6_send_cmd(pi, CMD_PREFIX_RESET | PREFIX_IO16 | PREFIX_BLK);
}

//***************************************************************************

static void ppc6_wr_extout(struct pi_adapter *pi, u8 regdata)
{
	ppc6_send_cmd(pi, REG_VERSION | ACCESS_REG | ACCESS_WRITE);

	ppc6_wr_data_byte(pi, (u8)((regdata & 0x03) << 6));
}

//***************************************************************************

static int ppc6_open(struct pi_adapter *pi)
{
	int ret;

	ret = ppc6_select(pi);

	if (ret == 0)
		return(ret);

	PPC_FLAGS &= ~fifo_wait;

	ppc6_send_cmd(pi, ACCESS_REG | ACCESS_WRITE | REG_RAMSIZE);
	ppc6_wr_data_byte(pi, RAMSIZE_128K);

	ppc6_send_cmd(pi, ACCESS_REG | ACCESS_READ | REG_VERSION);

	if ((ppc6_rd_data_byte(pi) & 0x3F) == 0x0C)
		PPC_FLAGS |= fifo_wait;

	return(ret);
}

//***************************************************************************

static void ppc6_close(struct pi_adapter *pi)
{
	ppc6_deselect(pi);
}

//***************************************************************************

