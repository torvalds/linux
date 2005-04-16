/*          Definitions for DigiBoard ditty(1) command.                 */

#if !defined(TIOCMODG)
#define	TIOCMODG	('d'<<8) | 250		/* get modem ctrl state	*/
#define	TIOCMODS	('d'<<8) | 251		/* set modem ctrl state	*/
#endif

#if !defined(TIOCMSET)
#define	TIOCMSET	('d'<<8) | 252		/* set modem ctrl state	*/
#define	TIOCMGET	('d'<<8) | 253		/* set modem ctrl state	*/
#endif

#if !defined(TIOCMBIC)
#define	TIOCMBIC	('d'<<8) | 254		/* set modem ctrl state */
#define	TIOCMBIS	('d'<<8) | 255		/* set modem ctrl state */
#endif

#if !defined(TIOCSDTR)
#define	TIOCSDTR	('e'<<8) | 0		/* set DTR		*/
#define	TIOCCDTR	('e'<<8) | 1		/* clear DTR		*/
#endif

/************************************************************************
 * Ioctl command arguments for DIGI parameters.
 ************************************************************************/
#define DIGI_GETA	('e'<<8) | 94		/* Read params		*/

#define DIGI_SETA	('e'<<8) | 95		/* Set params		*/
#define DIGI_SETAW	('e'<<8) | 96		/* Drain & set params	*/
#define DIGI_SETAF	('e'<<8) | 97		/* Drain, flush & set params */

#define	DIGI_GETFLOW	('e'<<8) | 99		/* Get startc/stopc flow */
						/* control characters 	 */
#define	DIGI_SETFLOW	('e'<<8) | 100		/* Set startc/stopc flow */
						/* control characters	 */
#define	DIGI_GETAFLOW	('e'<<8) | 101		/* Get Aux. startc/stopc */
						/* flow control chars 	 */
#define	DIGI_SETAFLOW	('e'<<8) | 102		/* Set Aux. startc/stopc */
						/* flow control chars	 */

#define	DIGI_GETINFO	('e'<<8) | 103		/* Fill in digi_info */
#define	DIGI_POLLER	('e'<<8) | 104		/* Turn on/off poller */
#define	DIGI_INIT	('e'<<8) | 105		/* Allow things to run. */

struct	digiflow_struct 
{
	unsigned char	startc;				/* flow cntl start char	*/
	unsigned char	stopc;				/* flow cntl stop char	*/
};

typedef struct digiflow_struct digiflow_t;


/************************************************************************
 * Values for digi_flags 
 ************************************************************************/
#define DIGI_IXON	0x0001		/* Handle IXON in the FEP	*/
#define DIGI_FAST	0x0002		/* Fast baud rates		*/
#define RTSPACE		0x0004		/* RTS input flow control	*/
#define CTSPACE		0x0008		/* CTS output flow control	*/
#define DSRPACE		0x0010		/* DSR output flow control	*/
#define DCDPACE		0x0020		/* DCD output flow control	*/
#define DTRPACE		0x0040		/* DTR input flow control	*/
#define DIGI_FORCEDCD	0x0100		/* Force carrier		*/
#define	DIGI_ALTPIN	0x0200		/* Alternate RJ-45 pin config	*/
#define	DIGI_AIXON	0x0400		/* Aux flow control in fep	*/


/************************************************************************
 * Values for digiDload
 ************************************************************************/
#define NORMAL  0
#define PCI_CTL 1

#define SIZE8  0
#define SIZE16 1
#define SIZE32 2

/************************************************************************
 * Structure used with ioctl commands for DIGI parameters.
 ************************************************************************/
struct digi_struct 
{
	unsigned short	digi_flags;		/* Flags (see above)	*/
};

typedef struct digi_struct digi_t;

struct digi_info 
{
	unsigned long board;        /* Which board is this ? */
	unsigned char status;       /* Alive or dead */
	unsigned char type;         /* see epca.h */
	unsigned char subtype;      /* For future XEM, XR, etc ... */
	unsigned short numports;    /* Number of ports configured */
	unsigned char *port;        /* I/O Address */
	unsigned char *membase;     /* DPR Address */
	unsigned char *version;     /* For future ... */
	unsigned short windowData;  /* For future ... */
} ;
