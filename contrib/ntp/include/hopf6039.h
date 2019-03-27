/****************************************************************************/
/*      hopf6039.h                                                          */     
/*      hopf Elektronik 6039 PCI radio clock header                         */
/*      (c) 1999, 2000 Bernd Altmeier    <altmeier@ATLSoft.de>              */
/*      Rev. 1.00 Date 25.03.2000                                           */
/*      History:                                                            */
/****************************************************************************/

#ifndef _hopf6039_H_
#define _hopf6039_H_

#define HOPF_MAXVERSION			8
#define	HOPF_CNTR_MEM_LEN		0x7f
#define	HOPF_DATA_MEM_LEN		0x3ff	/* this is our memory size */

/* macros and definition for 32 to 16 to 8 bit conversion */

typedef unsigned long       DWORD;
typedef unsigned char       BYTE;
typedef unsigned short      WORD;

#define LOWORD(l)     ((WORD)(l))
#define HIWORD(l)     ((WORD)(((DWORD)(l) >> 16) & 0xFFFF))
#define LOBYTE(w)     ((BYTE)(w))
#define HIBYTE(w)     ((BYTE)(((WORD)(w) >> 8) & 0xFF))

/* iocntl codes for driver access */

#define HOPF_CLOCK_CMD_MASK 0xff000

#define HOPF_CLOCK_GET_LOCAL		0x10000 
#define HOPF_CLOCK_GET_UTC		0x20000
#define HOPF_CLOCK_GET_ANTENNA		0x30000
#define HOPF_CLOCK_GET_DIFFERENCE	0x40000
#define HOPF_CLOCK_GET_VERSION		0x50000
#define HOPF_CLOCK_GET_POSITION		0x60000
#define HOPF_CLOCK_GET_SATDATA		0x70000
#define HOPF_CLOCK_GET_SYSTEMBYTE	0x80000
#define HOPF_CLOCK_GET_IRIG		0x90000

#define HOPF_CLOCK_SET_DIFFERENCE	0x01000
#define HOPF_CLOCK_SET_ANTENNA		0x02000
#define HOPF_CLOCK_SET_TIME 		0x03000
#define HOPF_CLOCK_SET_POSITION		0x04000
#define HOPF_CLOCK_SET_SATMODE		0x05000
#define HOPF_CLOCK_SET_SYSTEMBYTE	0x06000
#define HOPF_CLOCK_SET_RESET		0x07000
#define HOPF_CLOCK_SET_IRIG		0x08000

/* clock command codes */

#define HOPF_CLOCK_HARDRESET		0x00008000
#define HOPF_CLOCK_SOFTRESET		0x00004000

/* sat-information */

typedef struct SatStat{    
	BYTE wVisible;    
	BYTE wMode;
	BYTE wSat0;
	BYTE wRat0;
	BYTE wSat1;
	BYTE wRat1;
	BYTE wSat2;
	BYTE wRat2;
	BYTE wSat3;
	BYTE wRat3;
	BYTE wSat4;
	BYTE wRat4;
	BYTE wSat5;
	BYTE wRat5;
	BYTE wSat6;
	BYTE wRat6;
	BYTE wSat7;
	BYTE wRat7;
} SatStat;

/* GPS position */

typedef struct GPSPos {  /* Position */
	long wAltitude;   
	long wLongitude;   
	long wLatitude;    
} GPSPos;

/* clock hardware version */

typedef struct ClockVersion {    
	char cVersion[255];  /* Hardware Version like " DCF-RECEIVER,   VERSION 01.01, DAT: 23.NOV.1999" */
	char dVersion[255];  /* Driver Version */
} ClockVersion;

/* hopftime what you think */

typedef struct HOPFTIME { 
    unsigned int wYear; 
    unsigned int wMonth; 
    unsigned int wDayOfWeek; 
    unsigned int wDay; 
    unsigned int wHour; 
    unsigned int wMinute; 
    unsigned int wSecond; 
    unsigned int wMilliseconds; 
    unsigned int wStatus; 
} HOPFTIME; 

/* DCF77 antenna alignment */

typedef struct DcfAntenne {    
	BYTE bStatus;    
	BYTE bStatus1;    
	WORD wAntValue;    
} DcfAntenne;

/* hopf PCI clock */

typedef struct hopfCard {
	char name[32];
	unsigned irq;
	unsigned long membase; /* without mmap */
	unsigned int port;

	int versionlen;
	char versionbuf[1024];
	char *version[HOPF_MAXVERSION];
	char cardname[32];
	int interrupt;
	void *mbase;		   /* this will be our memory base address */

} hopfCard;

typedef struct cardparams {
	unsigned int port;
	unsigned irq;
	int cardtype;
	int cardnr;
	unsigned int membase;
} cardparams;


#define WRITE_REGISTER		0x00
#define READ_REGISTER		0x01

#endif /* _hopf6039_H_ */
