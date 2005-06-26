/*******************************************************************************
*
*   (c) 1998 by Computone Corporation
*
********************************************************************************
*
*
*   PACKAGE:     Linux tty Device Driver for IntelliPort family of multiport
*                serial I/O controllers.
*
*   DESCRIPTION: Definition table for In-line and Bypass commands. Applicable
*                only when the standard loadware is active. (This is included
*                source code, not a separate compilation module.)
*
*******************************************************************************/

//------------------------------------------------------------------------------
//
// Revision History:
//
// 10 October 1991   MAG First Draft
//  7 November 1991  MAG Reflects additional commands.
// 24 February 1992  MAG Additional commands for 1.4.x loadware
// 11 March 1992     MAG Additional commands
// 30 March 1992     MAG Additional command: CMD_DSS_NOW
// 18 May 1992       MAG Discovered commands 39 & 40 must be at the end of a
//                       packet: affects implementation.
//------------------------------------------------------------------------------

//************
//* Includes *
//************

#include "i2cmd.h"   /* To get some bit-defines */

//------------------------------------------------------------------------------
// Here is the table of global arrays which represent each type of command
// supported in the IntelliPort standard loadware. See also i2cmd.h
// for a more complete explanation of what is going on.
//------------------------------------------------------------------------------

// Here are the various globals: note that the names are not used except through
// the macros defined in i2cmd.h. Also note that although they are character
// arrays here (for extendability) they are cast to structure pointers in the
// i2cmd.h macros. See i2cmd.h for flags definitions.

//                     Length Flags Command
static UCHAR ct02[] = { 1, BTH,     0x02                     }; // DTR UP
static UCHAR ct03[] = { 1, BTH,     0x03                     }; // DTR DN
static UCHAR ct04[] = { 1, BTH,     0x04                     }; // RTS UP
static UCHAR ct05[] = { 1, BTH,     0x05                     }; // RTS DN
static UCHAR ct06[] = { 1, BYP,     0x06                     }; // START FL
static UCHAR ct07[] = { 2, BTH,     0x07,0                   }; // BAUD
static UCHAR ct08[] = { 2, BTH,     0x08,0                   }; // BITS
static UCHAR ct09[] = { 2, BTH,     0x09,0                   }; // STOP
static UCHAR ct10[] = { 2, BTH,     0x0A,0                   }; // PARITY
static UCHAR ct11[] = { 2, BTH,     0x0B,0                   }; // XON
static UCHAR ct12[] = { 2, BTH,     0x0C,0                   }; // XOFF
static UCHAR ct13[] = { 1, BTH,     0x0D                     }; // STOP FL
static UCHAR ct14[] = { 1, BYP|VIP, 0x0E                     }; // ACK HOTK
//static UCHAR ct15[]={ 2, BTH|VIP, 0x0F,0                   }; // IRQ SET
static UCHAR ct16[] = { 2, INL,     0x10,0                   }; // IXONOPTS
static UCHAR ct17[] = { 2, INL,     0x11,0                   }; // OXONOPTS
static UCHAR ct18[] = { 1, INL,     0x12                     }; // CTSENAB
static UCHAR ct19[] = { 1, BTH,     0x13                     }; // CTSDSAB
static UCHAR ct20[] = { 1, INL,     0x14                     }; // DCDENAB
static UCHAR ct21[] = { 1, BTH,     0x15                     }; // DCDDSAB
static UCHAR ct22[] = { 1, BTH,     0x16                     }; // DSRENAB
static UCHAR ct23[] = { 1, BTH,     0x17                     }; // DSRDSAB
static UCHAR ct24[] = { 1, BTH,     0x18                     }; // RIENAB
static UCHAR ct25[] = { 1, BTH,     0x19                     }; // RIDSAB
static UCHAR ct26[] = { 2, BTH,     0x1A,0                   }; // BRKENAB
static UCHAR ct27[] = { 1, BTH,     0x1B                     }; // BRKDSAB
//static UCHAR ct28[]={ 2, BTH,     0x1C,0                   }; // MAXBLOKSIZE
//static UCHAR ct29[]={ 2, 0,       0x1D,0                   }; // reserved
static UCHAR ct30[] = { 1, INL,     0x1E                     }; // CTSFLOWENAB
static UCHAR ct31[] = { 1, INL,     0x1F                     }; // CTSFLOWDSAB
static UCHAR ct32[] = { 1, INL,     0x20                     }; // RTSFLOWENAB
static UCHAR ct33[] = { 1, INL,     0x21                     }; // RTSFLOWDSAB
static UCHAR ct34[] = { 2, BTH,     0x22,0                   }; // ISTRIPMODE
static UCHAR ct35[] = { 2, BTH|END, 0x23,0                   }; // SENDBREAK
static UCHAR ct36[] = { 2, BTH,     0x24,0                   }; // SETERRMODE
//static UCHAR ct36a[]={ 3, INL,    0x24,0,0                 }; // SET_REPLACE

// The following is listed for completeness, but should never be sent directly
// by user-level code. It is sent only by library routines in response to data
// movement.
//static UCHAR ct37[]={ 5, BYP|VIP, 0x25,0,0,0,0             }; // FLOW PACKET

// Back to normal
//static UCHAR ct38[] = {11, BTH|VAR, 0x26,0,0,0,0,0,0,0,0,0,0 }; // DEF KEY SEQ
//static UCHAR ct39[]={ 3, BTH|END, 0x27,0,0                 }; // OPOSTON
//static UCHAR ct40[]={ 1, BTH|END, 0x28                     }; // OPOSTOFF
static UCHAR ct41[] = { 1, BYP,     0x29                     }; // RESUME
//static UCHAR ct42[]={ 2, BTH,     0x2A,0                   }; // TXBAUD
//static UCHAR ct43[]={ 2, BTH,     0x2B,0                   }; // RXBAUD
//static UCHAR ct44[]={ 2, BTH,     0x2C,0                   }; // MS PING
//static UCHAR ct45[]={ 1, BTH,     0x2D                     }; // HOTENAB
//static UCHAR ct46[]={ 1, BTH,     0x2E                     }; // HOTDSAB
//static UCHAR ct47[]={ 7, BTH,     0x2F,0,0,0,0,0,0         }; // UNIX FLAGS
//static UCHAR ct48[]={ 1, BTH,     0x30                     }; // DSRFLOWENAB
//static UCHAR ct49[]={ 1, BTH,     0x31                     }; // DSRFLOWDSAB
//static UCHAR ct50[]={ 1, BTH,     0x32                     }; // DTRFLOWENAB
//static UCHAR ct51[]={ 1, BTH,     0x33                     }; // DTRFLOWDSAB
//static UCHAR ct52[]={ 1, BTH,     0x34                     }; // BAUDTABRESET
//static UCHAR ct53[] = { 3, BTH,     0x35,0,0                 }; // BAUDREMAP
static UCHAR ct54[] = { 3, BTH,     0x36,0,0                 }; // CUSTOMBAUD1
static UCHAR ct55[] = { 3, BTH,     0x37,0,0                 }; // CUSTOMBAUD2
static UCHAR ct56[] = { 2, BTH|END, 0x38,0                   }; // PAUSE
static UCHAR ct57[] = { 1, BYP,     0x39                     }; // SUSPEND
static UCHAR ct58[] = { 1, BYP,     0x3A                     }; // UNSUSPEND
static UCHAR ct59[] = { 2, BTH,     0x3B,0                   }; // PARITYCHK
static UCHAR ct60[] = { 1, INL|VIP, 0x3C                     }; // BOOKMARKREQ
//static UCHAR ct61[]={ 2, BTH,     0x3D,0                   }; // INTERNALLOOP
//static UCHAR ct62[]={ 2, BTH,     0x3E,0                   }; // HOTKTIMEOUT
static UCHAR ct63[] = { 2, INL,     0x3F,0                   }; // SETTXON
static UCHAR ct64[] = { 2, INL,     0x40,0                   }; // SETTXOFF
//static UCHAR ct65[]={ 2, BTH,     0x41,0                   }; // SETAUTORTS
//static UCHAR ct66[]={ 2, BTH,     0x42,0                   }; // SETHIGHWAT
//static UCHAR ct67[]={ 2, BYP,     0x43,0                   }; // STARTSELFL
//static UCHAR ct68[]={ 2, INL,     0x44,0                   }; // ENDSELFL
//static UCHAR ct69[]={ 1, BYP,     0x45                     }; // HWFLOW_OFF
//static UCHAR ct70[]={ 1, BTH,     0x46                     }; // ODSRFL_ENAB
//static UCHAR ct71[]={ 1, BTH,     0x47                     }; // ODSRFL_DSAB
//static UCHAR ct72[]={ 1, BTH,     0x48                     }; // ODCDFL_ENAB
//static UCHAR ct73[]={ 1, BTH,     0x49                     }; // ODCDFL_DSAB
//static UCHAR ct74[]={ 2, BTH,     0x4A,0                   }; // LOADLEVEL
//static UCHAR ct75[]={ 2, BTH,     0x4B,0                   }; // STATDATA
//static UCHAR ct76[]={ 1, BYP,     0x4C                     }; // BREAK_ON
//static UCHAR ct77[]={ 1, BYP,     0x4D                     }; // BREAK_OFF
//static UCHAR ct78[]={ 1, BYP,     0x4E                     }; // GETFC
static UCHAR ct79[] = { 2, BYP,     0x4F,0                   }; // XMIT_NOW
//static UCHAR ct80[]={ 4, BTH,     0x50,0,0,0               }; // DIVISOR_LATCH
//static UCHAR ct81[]={ 1, BYP,     0x51                     }; // GET_STATUS
//static UCHAR ct82[]={ 1, BYP,     0x52                     }; // GET_TXCNT
//static UCHAR ct83[]={ 1, BYP,     0x53                     }; // GET_RXCNT
//static UCHAR ct84[]={ 1, BYP,     0x54                     }; // GET_BOXIDS
//static UCHAR ct85[]={10, BYP,     0x55,0,0,0,0,0,0,0,0,0   }; // ENAB_MULT
//static UCHAR ct86[]={ 2, BTH,     0x56,0                   }; // RCV_ENABLE
static UCHAR ct87[] = { 1, BYP,     0x57                     }; // HW_TEST
//static UCHAR ct88[]={ 3, BTH,     0x58,0,0                 }; // RCV_THRESHOLD
static UCHAR ct89[]={ 1, BYP,     0x59                     }; // DSS_NOW
//static UCHAR ct90[]={ 3, BYP,     0x5A,0,0                 }; // Set SILO
//static UCHAR ct91[]={ 2, BYP,     0x5B,0                   }; // timed break

// Some composite commands as well
//static UCHAR cc01[]={ 2, BTH,     0x02,0x04                }; // DTR & RTS UP
//static UCHAR cc02[]={ 2, BTH,     0x03,0x05                }; // DTR & RTS DN

//********
//* Code *
//********

//******************************************************************************
// Function:   i2cmdUnixFlags(iflag, cflag, lflag)
// Parameters: Unix tty flags
//
// Returns:    Pointer to command structure
//
// Description:
//
// This routine sets the parameters of command 47 and returns a pointer to the
// appropriate structure.
//******************************************************************************
#if 0
cmdSyntaxPtr
i2cmdUnixFlags(unsigned short iflag,unsigned short cflag,unsigned short lflag)
{
	cmdSyntaxPtr pCM = (cmdSyntaxPtr) ct47;

	pCM->cmd[1] = (unsigned char)  iflag;
	pCM->cmd[2] = (unsigned char) (iflag >> 8);
	pCM->cmd[3] = (unsigned char)  cflag;
	pCM->cmd[4] = (unsigned char) (cflag >> 8);
	pCM->cmd[5] = (unsigned char)  lflag;
	pCM->cmd[6] = (unsigned char) (lflag >> 8);
	return pCM;
}
#endif  /*  0  */

//******************************************************************************
// Function:   i2cmdBaudDef(which, rate)
// Parameters: ?
//
// Returns:    Pointer to command structure
//
// Description:
//
// This routine sets the parameters of commands 54 or 55 (according to the
// argument which), and returns a pointer to the appropriate structure.
//******************************************************************************
static cmdSyntaxPtr
i2cmdBaudDef(int which, unsigned short rate)
{
	cmdSyntaxPtr pCM;

	switch(which)
	{
	case 1:
		pCM = (cmdSyntaxPtr) ct54;
		break;
	default:
	case 2:
		pCM = (cmdSyntaxPtr) ct55;
		break;
	}
	pCM->cmd[1] = (unsigned char) rate;
	pCM->cmd[2] = (unsigned char) (rate >> 8);
	return pCM;
}

