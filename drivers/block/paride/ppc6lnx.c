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


#define port_stb					1
#define port_afd					2
#define cmd_stb						port_afd
#define port_init					4
#define data_stb					port_init
#define port_sel					8
#define port_int					16
#define port_dir					0x20

#define ECR_EPP	0x80
#define ECR_BI	0x20

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

typedef struct ppc_storage {
	u16	lpt_addr;				// LPT base address
	u8	ppc_id;
	u8	mode;						// operating mode
					// 0 = PPC Uni SW
					// 1 = PPC Uni FW
					// 2 = PPC Bi SW
					// 3 = PPC Bi FW
					// 4 = EPP Byte
					// 5 = EPP Word
					// 6 = EPP Dword
	u8	ppc_flags;
	u8	org_data;				// original LPT data port contents
	u8	org_ctrl;				// original LPT control port contents
	u8	cur_ctrl;				// current control port contents
} Interface;

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

static int ppc6_select(Interface *ppc);
static void ppc6_deselect(Interface *ppc);
static void ppc6_send_cmd(Interface *ppc, u8 cmd);
static void ppc6_wr_data_byte(Interface *ppc, u8 data);
static u8 ppc6_rd_data_byte(Interface *ppc);
static u8 ppc6_rd_port(Interface *ppc, u8 port);
static void ppc6_wr_port(Interface *ppc, u8 port, u8 data);
static void ppc6_rd_data_blk(Interface *ppc, u8 *data, long count);
static void ppc6_wait_for_fifo(Interface *ppc);
static void ppc6_wr_data_blk(Interface *ppc, u8 *data, long count);
static void ppc6_rd_port16_blk(Interface *ppc, u8 port, u8 *data, long length);
static void ppc6_wr_port16_blk(Interface *ppc, u8 port, u8 *data, long length);
static void ppc6_wr_extout(Interface *ppc, u8 regdata);
static int ppc6_open(Interface *ppc);
static void ppc6_close(Interface *ppc);

//***************************************************************************

static int ppc6_select(Interface *ppc)
{
	u8 i, j, k;

	i = inb(ppc->lpt_addr + 1);

	if (i & 1)
		outb(i, ppc->lpt_addr + 1);

	ppc->org_data = inb(ppc->lpt_addr);

	ppc->org_ctrl = inb(ppc->lpt_addr + 2) & 0x5F; // readback ctrl

	ppc->cur_ctrl = ppc->org_ctrl;

	ppc->cur_ctrl |= port_sel;

	outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

	if (ppc->org_data == 'b')
		outb('x', ppc->lpt_addr);

	outb('b', ppc->lpt_addr);
	outb('p', ppc->lpt_addr);
	outb(ppc->ppc_id, ppc->lpt_addr);
	outb(~ppc->ppc_id,ppc->lpt_addr);

	ppc->cur_ctrl &= ~port_sel;

	outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

	ppc->cur_ctrl = (ppc->cur_ctrl & port_int) | port_init;

	outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

	i = ppc->mode & 0x0C;

	if (i == 0)
		i = (ppc->mode & 2) | 1;

	outb(i, ppc->lpt_addr);

	ppc->cur_ctrl |= port_sel;

	outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

	// DELAY

	ppc->cur_ctrl |= port_afd;

	outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

	j = ((i & 0x08) << 4) | ((i & 0x07) << 3);

	k = inb(ppc->lpt_addr + 1) & 0xB8;

	if (j == k)
	{
		ppc->cur_ctrl &= ~port_afd;

		outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

		k = (inb(ppc->lpt_addr + 1) & 0xB8) ^ 0xB8;

		if (j == k)
		{
			if (i & 4)	// EPP
				ppc->cur_ctrl &= ~(port_sel | port_init);
			else				// PPC/ECP
				ppc->cur_ctrl &= ~port_sel;

			outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

			return(1);
		}
	}

	outb(ppc->org_ctrl, ppc->lpt_addr + 2);

	outb(ppc->org_data, ppc->lpt_addr);

	return(0); // FAIL
}

//***************************************************************************

static void ppc6_deselect(Interface *ppc)
{
	if (ppc->mode & 4)	// EPP
		ppc->cur_ctrl |= port_init;
	else								// PPC/ECP
		ppc->cur_ctrl |= port_sel;

	outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

	outb(ppc->org_data, ppc->lpt_addr);

	outb((ppc->org_ctrl | port_sel), ppc->lpt_addr + 2);

	outb(ppc->org_ctrl, ppc->lpt_addr + 2);
}

//***************************************************************************

static void ppc6_send_cmd(Interface *ppc, u8 cmd)
{
	switch(ppc->mode)
	{
		case PPCMODE_UNI_SW :
		case PPCMODE_UNI_FW :
		case PPCMODE_BI_SW :
		case PPCMODE_BI_FW :
		{
			outb(cmd, ppc->lpt_addr);

			ppc->cur_ctrl ^= cmd_stb;

			outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

			break;
		}

		case PPCMODE_EPP_BYTE :
		case PPCMODE_EPP_WORD :
		case PPCMODE_EPP_DWORD :
		{
			outb(cmd, ppc->lpt_addr + 3);

			break;
		}
	}
}

//***************************************************************************

static void ppc6_wr_data_byte(Interface *ppc, u8 data)
{
	switch(ppc->mode)
	{
		case PPCMODE_UNI_SW :
		case PPCMODE_UNI_FW :
		case PPCMODE_BI_SW :
		case PPCMODE_BI_FW :
		{
			outb(data, ppc->lpt_addr);

			ppc->cur_ctrl ^= data_stb;

			outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

			break;
		}

		case PPCMODE_EPP_BYTE :
		case PPCMODE_EPP_WORD :
		case PPCMODE_EPP_DWORD :
		{
			outb(data, ppc->lpt_addr + 4);

			break;
		}
	}
}

//***************************************************************************

static u8 ppc6_rd_data_byte(Interface *ppc)
{
	u8 data = 0;

	switch(ppc->mode)
	{
		case PPCMODE_UNI_SW :
		case PPCMODE_UNI_FW :
		{
			ppc->cur_ctrl = (ppc->cur_ctrl & ~port_stb) ^ data_stb;

			outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

			// DELAY

			data = inb(ppc->lpt_addr + 1);

			data = ((data & 0x80) >> 1) | ((data & 0x38) >> 3);

			ppc->cur_ctrl |= port_stb;

			outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

			// DELAY

			data |= inb(ppc->lpt_addr + 1) & 0xB8;

			break;
		}

		case PPCMODE_BI_SW :
		case PPCMODE_BI_FW :
		{
			ppc->cur_ctrl |= port_dir;

			outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

			ppc->cur_ctrl = (ppc->cur_ctrl | port_stb) ^ data_stb;

			outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

			data = inb(ppc->lpt_addr);

			ppc->cur_ctrl &= ~port_stb;

			outb(ppc->cur_ctrl,ppc->lpt_addr + 2);

			ppc->cur_ctrl &= ~port_dir;

			outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

			break;
		}

		case PPCMODE_EPP_BYTE :
		case PPCMODE_EPP_WORD :
		case PPCMODE_EPP_DWORD :
		{
			outb((ppc->cur_ctrl | port_dir),ppc->lpt_addr + 2);

			data = inb(ppc->lpt_addr + 4);

			outb(ppc->cur_ctrl,ppc->lpt_addr + 2);

			break;
		}
	}

	return(data);
}

//***************************************************************************

static u8 ppc6_rd_port(Interface *ppc, u8 port)
{
	ppc6_send_cmd(ppc,(u8)(port | ACCESS_PORT | ACCESS_READ));

	return(ppc6_rd_data_byte(ppc));
}

//***************************************************************************

static void ppc6_wr_port(Interface *ppc, u8 port, u8 data)
{
	ppc6_send_cmd(ppc,(u8)(port | ACCESS_PORT | ACCESS_WRITE));

	ppc6_wr_data_byte(ppc, data);
}

//***************************************************************************

static void ppc6_rd_data_blk(Interface *ppc, u8 *data, long count)
{
	switch(ppc->mode)
	{
		case PPCMODE_UNI_SW :
		case PPCMODE_UNI_FW :
		{
			while(count)
			{
				u8 d;

				ppc->cur_ctrl = (ppc->cur_ctrl & ~port_stb) ^ data_stb;

				outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

				// DELAY

				d = inb(ppc->lpt_addr + 1);

				d = ((d & 0x80) >> 1) | ((d & 0x38) >> 3);

				ppc->cur_ctrl |= port_stb;

				outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

				// DELAY

				d |= inb(ppc->lpt_addr + 1) & 0xB8;

				*data++ = d;
				count--;
			}

			break;
		}

		case PPCMODE_BI_SW :
		case PPCMODE_BI_FW :
		{
			ppc->cur_ctrl |= port_dir;

			outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

			ppc->cur_ctrl |= port_stb;

			while(count)
			{
				ppc->cur_ctrl ^= data_stb;

				outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

				*data++ = inb(ppc->lpt_addr);
				count--;
			}

			ppc->cur_ctrl &= ~port_stb;

			outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

			ppc->cur_ctrl &= ~port_dir;

			outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

			break;
		}

		case PPCMODE_EPP_BYTE :
		{
			outb((ppc->cur_ctrl | port_dir), ppc->lpt_addr + 2);

			// DELAY

			while(count)
			{
				*data++ = inb(ppc->lpt_addr + 4);
				count--;
			}

			outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

			break;
		}

		case PPCMODE_EPP_WORD :
		{
			outb((ppc->cur_ctrl | port_dir), ppc->lpt_addr + 2);

			// DELAY

			while(count > 1)
			{
				*((u16 *)data) = inw(ppc->lpt_addr + 4);
				data  += 2;
				count -= 2;
			}

			while(count)
			{
				*data++ = inb(ppc->lpt_addr + 4);
				count--;
			}

			outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

			break;
		}

		case PPCMODE_EPP_DWORD :
		{
			outb((ppc->cur_ctrl | port_dir),ppc->lpt_addr + 2);

			// DELAY

			while(count > 3)
			{
				*((u32 *)data) = inl(ppc->lpt_addr + 4);
				data  += 4;
				count -= 4;
			}

			while(count)
			{
				*data++ = inb(ppc->lpt_addr + 4);
				count--;
			}

			outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

			break;
		}
	}

}

//***************************************************************************

static void ppc6_wait_for_fifo(Interface *ppc)
{
	int i;

	if (ppc->ppc_flags & fifo_wait)
	{
		for(i=0; i<20; i++)
			inb(ppc->lpt_addr + 1);
	}
}

//***************************************************************************

static void ppc6_wr_data_blk(Interface *ppc, u8 *data, long count)
{
	switch(ppc->mode)
	{
		case PPCMODE_UNI_SW :
		case PPCMODE_BI_SW :
		{
			while(count--)
			{
				outb(*data++, ppc->lpt_addr);

				ppc->cur_ctrl ^= data_stb;

				outb(ppc->cur_ctrl, ppc->lpt_addr + 2);
			}

			break;
		}

		case PPCMODE_UNI_FW :
		case PPCMODE_BI_FW :
		{
			u8 this, last;

			ppc6_send_cmd(ppc,(CMD_PREFIX_SET | PREFIX_FASTWR));

			ppc->cur_ctrl |= port_stb;

			outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

			last = *data;

			outb(last, ppc->lpt_addr);

			while(count)
			{
				this = *data++;
				count--;

				if (this == last)
				{
					ppc->cur_ctrl ^= data_stb;

					outb(ppc->cur_ctrl, ppc->lpt_addr + 2);
				}
				else
				{
					outb(this, ppc->lpt_addr);

					last = this;
				}
			}

			ppc->cur_ctrl &= ~port_stb;

			outb(ppc->cur_ctrl, ppc->lpt_addr + 2);

			ppc6_send_cmd(ppc,(CMD_PREFIX_RESET | PREFIX_FASTWR));

			break;
		}

		case PPCMODE_EPP_BYTE :
		{
			while(count)
			{
				outb(*data++,ppc->lpt_addr + 4);
				count--;
			}

			ppc6_wait_for_fifo(ppc);

			break;
		}

		case PPCMODE_EPP_WORD :
		{
			while(count > 1)
			{
				outw(*((u16 *)data),ppc->lpt_addr + 4);
				data  += 2;
				count -= 2;
			}

			while(count)
			{
				outb(*data++,ppc->lpt_addr + 4);
				count--;
			}

			ppc6_wait_for_fifo(ppc);

			break;
		}

		case PPCMODE_EPP_DWORD :
		{
			while(count > 3)
			{
				outl(*((u32 *)data),ppc->lpt_addr + 4);
				data  += 4;
				count -= 4;
			}

			while(count)
			{
				outb(*data++,ppc->lpt_addr + 4);
				count--;
			}

			ppc6_wait_for_fifo(ppc);

			break;
		}
	}
}

//***************************************************************************

static void ppc6_rd_port16_blk(Interface *ppc, u8 port, u8 *data, long length)
{
	length = length << 1;

	ppc6_send_cmd(ppc, (REG_BLKSIZE | ACCESS_REG | ACCESS_WRITE));
	ppc6_wr_data_byte(ppc,(u8)length);
	ppc6_wr_data_byte(ppc,(u8)(length >> 8));
	ppc6_wr_data_byte(ppc,0);

	ppc6_send_cmd(ppc, (CMD_PREFIX_SET | PREFIX_IO16 | PREFIX_BLK));

	ppc6_send_cmd(ppc, (u8)(port | ACCESS_PORT | ACCESS_READ));

	ppc6_rd_data_blk(ppc, data, length);

	ppc6_send_cmd(ppc, (CMD_PREFIX_RESET | PREFIX_IO16 | PREFIX_BLK));
}

//***************************************************************************

static void ppc6_wr_port16_blk(Interface *ppc, u8 port, u8 *data, long length)
{
	length = length << 1;

	ppc6_send_cmd(ppc, (REG_BLKSIZE | ACCESS_REG | ACCESS_WRITE));
	ppc6_wr_data_byte(ppc,(u8)length);
	ppc6_wr_data_byte(ppc,(u8)(length >> 8));
	ppc6_wr_data_byte(ppc,0);

	ppc6_send_cmd(ppc, (CMD_PREFIX_SET | PREFIX_IO16 | PREFIX_BLK));

	ppc6_send_cmd(ppc, (u8)(port | ACCESS_PORT | ACCESS_WRITE));

	ppc6_wr_data_blk(ppc, data, length);

	ppc6_send_cmd(ppc, (CMD_PREFIX_RESET | PREFIX_IO16 | PREFIX_BLK));
}

//***************************************************************************

static void ppc6_wr_extout(Interface *ppc, u8 regdata)
{
	ppc6_send_cmd(ppc,(REG_VERSION | ACCESS_REG | ACCESS_WRITE));

	ppc6_wr_data_byte(ppc, (u8)((regdata & 0x03) << 6));
}

//***************************************************************************

static int ppc6_open(Interface *ppc)
{
	int ret;

	ret = ppc6_select(ppc);

	if (ret == 0)
		return(ret);

	ppc->ppc_flags &= ~fifo_wait;

	ppc6_send_cmd(ppc, (ACCESS_REG | ACCESS_WRITE | REG_RAMSIZE));
	ppc6_wr_data_byte(ppc, RAMSIZE_128K);

	ppc6_send_cmd(ppc, (ACCESS_REG | ACCESS_READ | REG_VERSION));

	if ((ppc6_rd_data_byte(ppc) & 0x3F) == 0x0C)
		ppc->ppc_flags |= fifo_wait;

	return(ret);
}

//***************************************************************************

static void ppc6_close(Interface *ppc)
{
	ppc6_deselect(ppc);
}

//***************************************************************************

