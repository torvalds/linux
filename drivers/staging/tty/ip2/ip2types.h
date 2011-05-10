/*******************************************************************************
*
*   (c) 1998 by Computone Corporation
*
********************************************************************************
*
*
*   PACKAGE:     Linux tty Device Driver for IntelliPort II family of multiport
*                serial I/O controllers.
*
*   DESCRIPTION: Driver constants and type definitions.
*
*   NOTES:
*
*******************************************************************************/
#ifndef IP2TYPES_H
#define IP2TYPES_H

//*************
//* Constants *
//*************

// Define some limits for this driver. Ports per board is a hardware limitation
// that will not change. Current hardware limits this to 64 ports per board.
// Boards per driver is a self-imposed limit.
//
#define IP2_MAX_BOARDS        4
#define IP2_PORTS_PER_BOARD   ABS_MOST_PORTS
#define IP2_MAX_PORTS         (IP2_MAX_BOARDS*IP2_PORTS_PER_BOARD)

#define ISA    0
#define PCI    1
#define EISA   2

//********************
//* Type Definitions *
//********************

typedef struct tty_struct *   PTTY;
typedef wait_queue_head_t   PWAITQ;

typedef unsigned char         UCHAR;
typedef unsigned int          UINT;
typedef unsigned short        USHORT;
typedef unsigned long         ULONG;

typedef struct 
{
	short irq[IP2_MAX_BOARDS]; 
	unsigned short addr[IP2_MAX_BOARDS];
	int type[IP2_MAX_BOARDS];
#ifdef CONFIG_PCI
	struct pci_dev *pci_dev[IP2_MAX_BOARDS];
#endif
} ip2config_t;

#endif
