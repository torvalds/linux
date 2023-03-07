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

static void ppc6_wr_data_byte(struct pi_adapter *pi, u8 data);

//***************************************************************************

int mode_map[] = { PPCMODE_UNI_FW, PPCMODE_BI_FW, PPCMODE_EPP_BYTE,
		   PPCMODE_EPP_WORD, PPCMODE_EPP_DWORD };

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
